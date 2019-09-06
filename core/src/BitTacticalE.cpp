
#include <core/BitTacticalE.h>

namespace core {

    /* AUXILIARY FUNCTIONS */

    template <typename T>
    uint8_t BitTacticalE<T>::computeTacticalEBitsPE(uint16_t act, uint16_t wgt, const int network_bits) {

        #ifdef ZERO_COUNT
        if(wgt == 0) return 1;
        #else
        if(wgt == 0) return 0;
        #endif

        uint16_t act_bits = act;

        #ifdef BOOTH_ENCODING
        act_bits = this->booth_encoding(act_bits);
        #endif

        uint8_t act_effectual_bits = this->effectualBits(act_bits);

        uint8_t bit_multiplications = act_effectual_bits * (uint8_t)network_bits;
        #ifdef ZERO_COUNT
        if(bit_multiplications == 0) bit_multiplications = 1;
        #endif

        return bit_multiplications;
    }

    template <typename T>
    uint8_t BitTacticalE<T>::computeTacticalEPE(const std::vector<std::queue<uint8_t>> &offsets) {

        // Two stages shifting
        uint8_t PE_cycles = 0;
        auto tmp_offsets = offsets;
        auto max_offset_first_stage = (uint8_t)((1u << BITS_FIRST_STAGE) - 1);

        bool still_ones = this->check_act_bits(tmp_offsets);
        while (still_ones) {

            // Get the offset for the second stage shift
            std::vector<uint8_t> last_bit;
            for (const auto &act_bits : tmp_offsets) {
                if(!act_bits.empty()) last_bit.push_back(act_bits.front());
            }
            // Must be one to enter the while loop
            uint8_t two_stage_offset = *std::min_element(last_bit.begin(), last_bit.end());
            auto max_offset = two_stage_offset + max_offset_first_stage;

            //Update values
            for (auto &act_bits : tmp_offsets) {
                if(!act_bits.empty() && act_bits.front() <= max_offset) act_bits.pop();
            }

            PE_cycles++;
            still_ones = this->check_act_bits(tmp_offsets);
        }

        #ifdef ZERO_COUNT
        if(PE_cycles == 0) PE_cycles = 1;
        #endif

        return PE_cycles;
    }

    template <typename T>
    uint8_t BitTacticalE<T>::computeTacticalEColumn(int batch, int recursion, int act_x, int act_y, int stride,
            const cnpy::Array<T> &padded_act, const schedule &dense_schedule, int schedule_time, bool lstm) {

        std::list<uint16_t> unique_act_bits;
        std::vector<std::queue<uint8_t>> offsets;
        for (int row = 0; row < this->N_ROWS; row++) {
            for (int wgt_idx = 0; wgt_idx < this->N_LANES; wgt_idx++) {

                int pos = row * this->N_LANES + wgt_idx;
                auto wgt_tuple = dense_schedule[schedule_time][pos];
                int channel = std::get<0>(wgt_tuple);
                int kernel_x = std::get<1>(wgt_tuple);
                int kernel_y = std::get<2>(wgt_tuple);
                if(channel < 0) continue;

                T act_bits;
                if(lstm)
                    act_bits = padded_act.get(recursion, batch, channel);
                else
                    act_bits = padded_act.get(batch, channel, stride * act_x + kernel_x, stride * act_y + kernel_y);

                #ifdef BOOTH_ENCODING
                act_bits = this->booth_encoding(act_bits);
                #endif

                // Only store different activations
                auto it = std::find(unique_act_bits.begin(), unique_act_bits.end(), act_bits);
                if(it == unique_act_bits.end()) unique_act_bits.push_back(act_bits);
                else continue;

                uint8_t count = 0;
                std::queue<uint8_t> act_offsets;
                while (act_bits) {
                    auto current_bit = act_bits & 1u;
                    if (current_bit) act_offsets.push(count);
                    act_bits >>= 1;
                    count++;
                }

                offsets.push_back(act_offsets);
            }
        }

        return computeTacticalEPE(offsets);

    }

    template <typename T>
    void BitTacticalE<T>::computeTacticalETile(int batch, const std::vector<int> &list_act_x,
            const std::vector<int> &list_act_y, int stride, const cnpy::Array<T> &padded_act,
            const schedule &dense_schedule, int schedule_time, std::vector<uint32_t> &cycles_per_col,
            std::vector<uint32_t> &end_previous_pallet, sys::Statistics::Stats &stats) {

        //Get the slowest column
        for(int window = 0; window < list_act_x.size(); window++) {
            uint8_t column_cycles = computeTacticalEColumn(batch,0,list_act_x[window],list_act_y[window], stride,
                    padded_act,dense_schedule,schedule_time,false);
            cycles_per_col[window] += column_cycles;
        }

        // Column registers
        if(this->COLUMN_REGISTERS > 0) {
            auto fastest_column = end_previous_pallet[0] + 1;
            for(auto &column_cycles : cycles_per_col) {
                if(column_cycles <= end_previous_pallet[0]) {
                    if(column_cycles < fastest_column) fastest_column = column_cycles;
                    column_cycles = end_previous_pallet[0] + 1;
                }
            }
            stats.stall_cycles.back()[batch] += (end_previous_pallet[0] + 1) - fastest_column;;

            //Update end_previous_pallet
            for(int i = 0; i < this->COLUMN_REGISTERS - 1; i++) {
                end_previous_pallet[i] = end_previous_pallet[i + 1];
            }
            end_previous_pallet[this->COLUMN_REGISTERS - 1] = *std::max_element(cycles_per_col.begin(),
                    cycles_per_col.end());
        } else {
            auto slowest_col = *std::max_element(cycles_per_col.begin(), cycles_per_col.end());
            auto fastest_col = *std::min_element(cycles_per_col.begin(), cycles_per_col.end());
            cycles_per_col = std::vector<uint32_t>(this->N_COLUMNS, slowest_col);
            stats.stall_cycles.back()[batch] += slowest_col - fastest_col;
        }

    }

    /* CYCLES */

    template <typename T>
    void BitTacticalE<T>::computeConvolution(const core::Layer<T> &layer, sys::Statistics::Stats &stats,
            const schedule &proto_dense_schedule) {

        std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();

        cnpy::Array<T> act = layer.getActivations();
        act.powers_of_two_representation(layer.getActPrecision());
        cnpy::Array<T> wgt = layer.getWeights();
        if(wgt.getDimensions() == 2) wgt.reshape_to_4D();

        int padding = layer.getPadding();
        int stride = layer.getStride();

        act.zero_pad(padding);

        if(act.getShape()[1] == 3 && stride > 1) {
            act.reshape_first_layer_act((uint16_t)stride);
            wgt.reshape_first_layer_wgt((uint16_t)stride);
            stride = 1;
        }

        const std::vector<size_t> &act_shape = act.getShape();
        const std::vector<size_t> &wgt_shape = wgt.getShape();

        auto batch_size = act_shape[0];
        auto act_channels = act_shape[1];
        auto Nx = act_shape[2];
        auto Ny = act_shape[3];
        if(this->FAST_MODE) batch_size = 1;

        auto num_filters = wgt_shape[0];
        auto wgt_channels = wgt_shape[1];
        auto Kx = wgt_shape[2];
        auto Ky = wgt_shape[3];

        long out_x = (Nx - Kx)/stride + 1;
        long out_y = (Ny - Ky)/stride + 1;

        auto groups = act_channels / wgt_channels;
        auto num_filters_sets = (uint32_t)ceil(num_filters/(double)this->N_ROWS/groups);

        // Stats
        stats.cycles.emplace_back(std::vector<uint64_t>(batch_size,0));
        stats.stall_cycles.emplace_back(std::vector<uint64_t>(batch_size,0));
        stats.weight_buff_reads.emplace_back(std::vector<uint64_t>(batch_size,0));
        stats.act_buff_reads.emplace_back(std::vector<uint64_t>(batch_size,0));
        stats.accumulator_updates.emplace_back(std::vector<uint64_t>(batch_size,0));
        stats.scheduled_pe.emplace_back(std::vector<uint64_t>(batch_size,0));
        stats.idle_pe.emplace_back(std::vector<uint64_t>(batch_size,0));

        int n;

        schedule dense_schedule;
        if(proto_dense_schedule.empty())
            dense_schedule = this->scheduler(wgt,act_channels);
        else
            dense_schedule = proto_dense_schedule;

        // Convolution
        #ifdef OPENMP
        auto max_threads = omp_get_max_threads();
        omp_set_num_threads(std::min(max_threads,this->N_THREADS));
        #pragma omp parallel for private(n)
        #endif
        for(n = 0; n < batch_size; n++) {

            std::vector<int> list_x, list_y;
            int x_counter = 0, y_counter = 0;
            std::vector<uint32_t> end_previous_pallet = std::vector<uint32_t>(this->COLUMN_REGISTERS, 0);
            std::vector<uint32_t> cycles_per_col = std::vector<uint32_t>(this->N_COLUMNS, 0);
            uint64_t weight_buff_reads = 0;
            uint64_t act_buff_reads = 0;
            uint64_t accumulator_updates = 0;
            uint64_t scheduled_pe = 0;
            uint64_t idle_pe = 0;

            while (this->iterateWindows(out_x, out_y, list_x, list_y, x_counter, y_counter, this->N_COLUMNS)) {
                for(int schedule_time = 0; schedule_time < dense_schedule.size(); schedule_time++) {
                    computeTacticalETile(n, list_x, list_y, stride, act, dense_schedule, schedule_time,
                            cycles_per_col, end_previous_pallet, stats);

                    act_buff_reads++;
                    weight_buff_reads++;
                    scheduled_pe += list_x.size() * this->N_ROWS;
                    idle_pe += (this->N_COLUMNS - list_x.size()) * this->N_ROWS;
                }
                accumulator_updates++;
            }

            stats.cycles.back()[n] = *std::max_element(cycles_per_col.begin(), cycles_per_col.end());
            stats.weight_buff_reads.back()[n] = weight_buff_reads;
            stats.act_buff_reads.back()[n] = act_buff_reads;
            stats.accumulator_updates.back()[n] = accumulator_updates * num_filters_sets;
            stats.scheduled_pe.back()[n] = scheduled_pe;
            stats.idle_pe.back()[n] = idle_pe;

        }

        std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);

        stats.time.push_back(time_span);

    }

    template <typename T>
    void BitTacticalE<T>::computeInnerProduct(const Layer<T> &layer, sys::Statistics::Stats &stats,
            const schedule &proto_dense_schedule) {

        std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();

        cnpy::Array<T> act = layer.getActivations();
        act.powers_of_two_representation(layer.getActPrecision());
        cnpy::Array<T> wgt = layer.getWeights();
        wgt.reshape_to_4D();

        if(layer.getType() == "InnerProduct") {
            if (act.getDimensions() == 4) act.reshape_to_2D();
            act.reshape_to_4D();
        }

        bool lstm = layer.getType() == "LSTM";

        const std::vector<size_t> &act_shape = act.getShape();
        const std::vector<size_t> &wgt_shape = wgt.getShape();

        uint64_t batch_size, act_channels, R;
        if(lstm) {
            R = act_shape[0];
            batch_size = act_shape[1];
            act_channels = act_shape[2];
        } else {
            R = 1;
            batch_size = act_shape[0];
            act_channels = act_shape[1];
        }
        if(this->FAST_MODE) batch_size = 1;

        auto num_filters = wgt_shape[0];

        auto num_filters_sets = (uint32_t)ceil(num_filters/(double)this->N_ROWS);

        // Stats
        stats.cycles.emplace_back(std::vector<uint64_t>(batch_size,0));
        stats.stall_cycles.emplace_back(std::vector<uint64_t>(batch_size,0));
        stats.weight_buff_reads.emplace_back(std::vector<uint64_t>(batch_size,0));
        stats.act_buff_reads.emplace_back(std::vector<uint64_t>(batch_size,0));
        stats.accumulator_updates.emplace_back(std::vector<uint64_t>(batch_size,0));
        stats.scheduled_pe.emplace_back(std::vector<uint64_t>(batch_size,0));
        stats.idle_pe.emplace_back(std::vector<uint64_t>(batch_size,0));

        int n;

        schedule dense_schedule;
        if(proto_dense_schedule.empty())
            dense_schedule = this->scheduler(wgt,act_channels);
        else
            dense_schedule = proto_dense_schedule;

        #ifndef FC_MULTIPLEX_COLUMNS

        // All FC in one column
        #ifdef OPENMP
        auto max_threads = omp_get_max_threads();
        omp_set_num_threads(std::min(max_threads,this->N_THREADS));
        #pragma omp parallel for private(n)
        #endif
        for (n = 0; n < batch_size; n++) {

            uint64_t cycles = 0;
            uint64_t weight_buff_reads = 0;
            uint64_t act_buff_reads = 0;
            uint64_t accumulator_updates = 0;

            for (int r = 0; r < R; r++) {
                for(int schedule_time = 0; schedule_time < dense_schedule.size(); schedule_time++) {
                    cycles += computeTacticalEColumn(n,r,0,0,0,act,dense_schedule,schedule_time,lstm);
                    weight_buff_reads++;
                    act_buff_reads++;
                }
                accumulator_updates++;
            }

            stats.cycles.back()[n] = cycles;
            stats.weight_buff_reads.back()[n] = weight_buff_reads;
            stats.act_buff_reads.back()[n] = act_buff_reads;
            stats.accumulator_updates.back()[n] = accumulator_updates;
            stats.scheduled_pe.back()[n] = num_filters * this->N_ROWS * ceil(act_channels/(double)WEIGHT_LANES);
            auto idle_rows = this->N_ROWS - (num_filters % this->N_ROWS);
            idle_rows = idle_rows == 16 ? 0 : idle_rows;
            stats.idle_pe.back()[n] = idle_rows * ceil(act_channels/(double)WEIGHT_LANES);

        }

        #else

        #ifdef OPENMP
        auto max_threads = omp_get_max_threads();
        omp_set_num_threads(std::min(max_threads,this->N_THREADS));
        #pragma omp parallel for private(n)
        #endif
        for (n = 0; n < batch_size; n++) {

            int column_index = 0;
            std::vector<uint64_t> column_end = std::vector<uint64_t>(this->N_COLUMNS, 0);
            uint64_t cycles = 0;
            uint64_t stall_cycles = 0;
            uint64_t weight_buff_reads = 0;
            uint64_t act_buff_reads = 0;
            uint64_t accumulator_updates = 0;

            for (int r = 0; r < R; r++) {
                for(int schedule_time = 0; schedule_time < dense_schedule.size(); schedule_time++) {
                    if(cycles < column_end[column_index]) {
                        stall_cycles = column_end[column_index] - cycles;
                        cycles = column_end[column_index];
                    }
                    auto column_cycles = computeTacticalEColumn(n,r,0,0,0,act,dense_schedule,schedule_time,lstm);
                    column_end[column_index] = cycles + column_cycles;
                    cycles++;
                    column_index++;
                    if(column_index >= this->N_COLUMNS) column_index = 0;

                    act_buff_reads++;
                    weight_buff_reads++;
                }
                accumulator_updates++;
            }

            uint64_t last_column_end = *std::max_element(column_end.begin(), column_end.end());
            stats.cycles.back()[n] = std::max(cycles, last_column_end);
            stats.stall_cycles.back()[n] = stall_cycles;
            stats.weight_buff_reads.back()[n] = weight_buff_reads;
            stats.act_buff_reads.back()[n] = act_buff_reads;
            stats.accumulator_updates.back()[n] = accumulator_updates * num_filters_sets;
            stats.scheduled_pe.back()[n] = (uint64_t)(num_filters * this->N_ROWS *
                    ceil(act_channels/(double)this->N_LANES));
            auto idle_rows = this->N_ROWS - (num_filters % this->N_ROWS);
            idle_rows = idle_rows == 16 ? 0 : idle_rows;
            stats.idle_pe.back()[n] = (uint64_t)(idle_rows * ceil(act_channels/(double)this->N_LANES));

        }

        #endif

        std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);

        stats.time.push_back(time_span);

    }

    template <typename T>
    void BitTacticalE<T>::run(const Network<T> &network) {
        // Initialize statistics
        sys::Statistics::Stats stats;
        sys::Statistics::initialize(stats);

        stats.task_name = "cycles";
        stats.net_name = network.getName();
        int mux_entries = this->LOOKAHEAD_H + this->LOOKASIDE_D + 1;
        stats.arch = "BitTacticalE_C" + std::to_string(this->N_COLUMNS) + "_R" + std::to_string(this->N_ROWS) + "_B" +
                std::to_string(BITS_FIRST_STAGE) + "_CR" + std::to_string(this->COLUMN_REGISTERS) + "_" +
                this->SEARCH_SHAPE + std::to_string(mux_entries) + "(" + std::to_string(this->LOOKAHEAD_H) + "-" +
                std::to_string(this->LOOKASIDE_D) + ")";

        for(const Layer<T> &layer : network.getLayers()) {
            if(layer.getType() == "Convolution") {
                stats.layers.push_back(layer.getName());
                computeConvolution(layer, stats, schedule());
            } else if(layer.getType() == "InnerProduct" || layer.getType() == "LSTM") {
                stats.layers.push_back(layer.getName());
                computeInnerProduct(layer, stats, schedule());
            }
        }
        // Set statistics to write
        sys::Statistics::addStats(stats);
    }

    template <typename T>
    void BitTacticalE<T>::run(const Network<T> &network, const std::vector<schedule> &schedules) {
        // Initialize statistics
        sys::Statistics::Stats stats;
        sys::Statistics::initialize(stats);

        stats.task_name = "cycles";
        stats.net_name = network.getName();
        int mux_entries = this->LOOKAHEAD_H + this->LOOKASIDE_D + 1;
        stats.arch = "BitTacticalE_C" + std::to_string(this->N_COLUMNS) + "_R" + std::to_string(this->N_ROWS) + "_B" +
                std::to_string(BITS_FIRST_STAGE) + "_CR" + std::to_string(this->COLUMN_REGISTERS) + "_" +
                this->SEARCH_SHAPE + std::to_string(mux_entries) + "(" + std::to_string(this->LOOKAHEAD_H) + "-" +
                std::to_string(this->LOOKASIDE_D) + ")";

        int sch_index = 0;
        for(const Layer<T> &layer : network.getLayers()) {
            if(layer.getType() == "Convolution") {
                const schedule &proto_dense_schedule = schedules[sch_index];
                stats.layers.push_back(layer.getName());
                computeConvolution(layer, stats, proto_dense_schedule);
                sch_index++;
            } else if(layer.getType() == "InnerProduct" || layer.getType() == "LSTM") {
                const schedule &proto_dense_schedule = schedules[sch_index];
                stats.layers.push_back(layer.getName());
                computeInnerProduct(layer, stats, proto_dense_schedule);
                sch_index++;
            }
        }
        // Set statistics to write
        sys::Statistics::addStats(stats);
    }

    /* POTENTIALS */

    template <typename T>
    void BitTacticalE<T>::potentials(const Network<T> &network) {

        // Initialize statistics
        std::string filename = network.getName() + "_BitTacticalE_potentials";
        sys::Stats stats = sys::Stats(network.getNumLayers(), network.getBatches(), filename);

        auto bit_multiplications = stats.register_uint_t("bit_multiplications", 0, sys::AverageTotal);
        auto work_reduction = stats.register_double_t("work_reduction", 0, sys::Average);
        auto speedup = stats.register_double_t("speedup", 0, sys::Average);
        auto par_mult = stats.register_double_t("parallel_multiplication", 0, sys::AverageTotal);
        auto act_prec = stats.register_uint_t("activations_precision", 0, sys::Average);
        auto wgt_prec = stats.register_uint_t("weights_precision", 0, sys::Average);

        for(auto layer_it = 0; layer_it < network.getLayers().size(); ++layer_it) {

            const Layer<T> &layer = network.getLayers()[layer_it];
            bool conv = layer.getType() == "Convolution";
            bool lstm = layer.getType() == "LSTM";

            cnpy::Array<T> act = layer.getActivations();
            if (!conv && act.getDimensions() == 4) act.reshape_to_2D();
            act.powers_of_two_representation(layer.getActPrecision());
            cnpy::Array<T> wgt = layer.getWeights();

            int padding = layer.getPadding();
            int stride = layer.getStride();

            if (conv) act.zero_pad(padding);

            const std::vector<size_t> &act_shape = act.getShape();
            const std::vector<size_t> &wgt_shape = wgt.getShape();

            uint64_t batch_size, act_channels, Nx, Ny, R;
            if (lstm) {
                R = act_shape[0];
                batch_size = act_shape[1];
                act_channels = act_shape[2];
                Nx = 1;
                Ny = 1;
            } else {
                R = 1;
                batch_size = act_shape[0];
                act_channels = act_shape[1];
                Nx = act_shape[2];
                Ny = act_shape[3];
            }

            auto num_filters = wgt_shape[0];
            auto wgt_channels = wgt_shape[1];
            auto Kx = wgt_shape[2];
            auto Ky = wgt_shape[3];

            long out_x = (Nx - Kx + 2*padding)/stride + 1;
            long out_y = (Ny - Ky + 2*padding)/stride + 1;

            auto groups = act_channels / wgt_channels;
            auto it_per_group = num_filters / groups;

            auto network_bits = network.getNetwork_bits();

            // Operations
            uint64_t parallel_mult = conv ? num_filters * out_x * out_y * Kx * Ky * wgt_channels :
                    num_filters * wgt_channels * R;
            uint64_t bit_counter = 0;

            int n;

            #ifdef OPENMP
            auto max_threads = omp_get_max_threads();
            omp_set_num_threads(std::min(max_threads,this->N_THREADS));
            #pragma omp parallel for private(n)
            #endif
            for(n = 0; n < batch_size; n++) {
                double MAX_BITS = network_bits * network_bits;
                bit_counter = 0;

                if (conv) {

                    for(int m = 0; m < num_filters; m++) {

                        // Two towers alexnet
                        int start_group = 0;
                        if(m >= it_per_group)
                            start_group = (int)wgt_channels;

                        // Fix for MobileNet
                        if(wgt_channels == 1 && act_channels != 1)
                            start_group = m;

                        for(int x = 0; x < out_x; x++) {
                            for(int y = 0; y < out_y; y++) {
                                for(int i = 0; i < Kx; i++) {
                                    for(int j = 0; j < Ky; j++) {
                                        for(int k = 0; k < wgt_channels; k++) {
                                            bit_counter += computeTacticalEBitsPE(act.get(n, start_group + k,
                                                    stride * x + i, stride * y + j),wgt.get(m, k, i, j), network_bits);
                                        }
                                    }
                                }
                            }
                        }
                    }

                } else {

                    for (int r = 0; r < R; r++) {
                        for (int m = 0; m < num_filters; m++) {
                            for (int k = 0; k < wgt_channels; k++) {
                                auto act_bits = lstm ? act.get(r, n, k) : act.get(n, k);
                                bit_counter += computeTacticalEBitsPE(act_bits, wgt.get(m, k), network_bits);
                            }
                        }
                    }

                }


                bit_multiplications->value[layer_it][n] = bit_counter;
                work_reduction->value[layer_it][n] = 100 - ((double)bit_counter / (double)parallel_mult / MAX_BITS * 100);
                speedup->value[layer_it][n] = (double)parallel_mult * MAX_BITS / (double)bit_counter;
                par_mult->value[layer_it][n] = parallel_mult;
            }

        }

        //Dump statistics
        stats.dump_csv(network.getName(), network.getLayersName());
    }

    template class BitTacticalE<uint16_t>;

}