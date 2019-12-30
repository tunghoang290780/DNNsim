
#include <core/Stripes.h>

namespace core {

    /* AUXILIARY FUNCTIONS */

    template <typename T>
    uint64_t Stripes<T>::getCycles() const {
        return this->linear ? sys::get_max(this->column_cycles[0]) : this->cycles;
    }

    template <typename T>
    std::string Stripes<T>::name() {
        return "Stripes";
    }

    /* CYCLES */

    template <typename T>
    std::string Stripes<T>::filename() {
        return "";
    }

    template <typename T>
    std::string Stripes<T>::header() {
        return "";
    }

    template <typename T>
    bool Stripes<T>::schedule() {
        return false;
    }

    template <typename T>
    void Stripes<T>::process_tiles(const std::vector<TileData<T>> &tiles_data, int act_prec, int wgt_prec) {

        if (this->linear) {

            if(this->cycles < this->column_cycles[0][this->column_index]) {
                this->stall_cycles += this->column_cycles[0][this->column_index] - this->cycles;
                this->cycles = this->column_cycles[0][this->column_index];
            }

            this->column_cycles[0][this->column_index] = this->cycles + act_prec;
            this->column_index = (this->column_index + 1) % this->column_cycles.size();
            this->cycles++;

        } else {
            this->cycles += act_prec;
        }

        for (const auto &tile_data : tiles_data) {

            if (!tile_data.valid)
                continue;

            auto ROWS = tile_data.wgt_row.size() / tile_data.lanes;
            auto COLUMNS = tile_data.wgt_row.size() / tile_data.lanes;
            if (this->linear) {
                this->scheduled_pe += tile_data.filters.size();
                this->idle_pe += ROWS - tile_data.filters.size();
            } else {
                this->scheduled_pe += tile_data.windows.size() * tile_data.filters.size();
                this->idle_pe += (COLUMNS * ROWS - tile_data.windows.size() * tile_data.filters.size());
            }
        }

    }

    /* POTENTIALS */

    template <typename T>
    std::string Stripes<T>::filename_pot() {
        return "";
    }

    template <typename T>
    std::string Stripes<T>::header_pot() {
        return "";
    }

    template <typename T>
    uint16_t Stripes<T>::computeBits(T act, T wgt, uint8_t act_prec, uint8_t wgt_prec, uint8_t network_bits) {
        return act_prec * network_bits;
    }

    template class Stripes<uint16_t>;

}
