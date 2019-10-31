#ifndef DNNSIM_BATCH_H
#define DNNSIM_BATCH_H

#include <sys/common.h>
#include <batch.pb.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>

namespace sys {

    /**
     * Batch configuration
     */
    class Batch {

    public:

        /** Struct for the Simulate instructions */
        struct Simulate {

            /** Struct for the Experiments configurations */
            struct Experiment {

                /** Architecture name */
                std::string architecture = "";

                /** Task name */
                std::string task = "";

                /** Number of lanes */
                uint32_t n_lanes = 0;

                /** Number of columns */
                uint32_t n_columns = 0;

                /** Number of rows */
                uint32_t n_rows = 0;

                /** Number of tiles */
                uint32_t n_tiles = 0;

                /** Bits per PE */
                uint32_t bits_pe = 0;

                /** Column registers */
                uint32_t column_registers = 0;

                /** Precision granularity */
                uint32_t precision_granularity = 0;

                /** Leading bit */
                bool leading_bit = false;

                /** Minor bit */
                bool minor_bit = false;

                /** Bit first shift stage: Pragmatic PE */
                uint32_t bits_first_stage = 0;

                /** Lookahead H: Tactical */
                uint32_t lookahead_h = 0;

                /** Lookaside D: Tactical */
                uint32_t lookaside_d = 0;

                /** Search shape: Tactical */
                char search_shape = 'X';

                /** Read schedule from protobuf: Tactical */
                bool read_schedule = false;

                /** X Dim PEs: SCNN */
                uint32_t Wt = 0;

                /** Y Dim PEs: SCNN */
                uint32_t Ht = 0;

                /** X Dim multiplier pe PER: SCNN */
                uint32_t I = 0;

                /** Y Dim multiplier pe PER: SCNN */
                uint32_t F = 0;

                /** Output accumulator size: SCNN */
                uint32_t out_acc_size = 0;

                /** Output accumulator banks: SCNN */
                uint32_t banks = 0;

                /** Serial bits per PE */
                uint32_t pe_serial_bits = 0;

                /** Matrix width: BitFusion */
                uint32_t M = 0;

                /** Matrix height: BitFusion */
                uint32_t N = 0;

                /** Maximum precision: BitFusion */
                uint32_t pmax = 0;

                /** Minimum precision: BitFusion */
                uint32_t pmin = 0;

                /** Difft simulation */
                bool diffy = false;

                /** Dynamic width precision: Loom */
                bool dynamic_weights = false;

                /** Simulate baseline */
                bool baseline;

                /** On-chip activations size */
                uint64_t act_memory_size = 0;

                /** On-chip weights size */
                uint64_t wgt_memory_size = 0;

            };

            /** Batch number of the traces */
            uint32_t batch = 0;

            /** Number of epochs of the training traces */
            uint32_t epochs = 0;

            /** Model input type */
            std::string model = "";

            /** Data input type */
            std::string data_type = "";

            /** Network name */
            std::string network = "";

            /** Network bits */
            uint32_t network_bits = 0;

            /** Tensorflow quantization */
            bool tensorflow_8b = false;

            /** Intel INQ quantization */
            bool intel_inq = false;

            /** Training traces */
            bool training = false;

            /** Only forward traces */
            bool only_forward = false;

            /** Only backward traces */
            bool only_backward = false;

            /** Number of decoder states of Seq2Seq traces */
            uint32_t decoder_states = 0;

            /** Activations does not have sign */
            bool unsigned_activations = false;

            /** Weights does not have sign */
            bool unsigned_weights = false;

            /** Array of experiments */
            std::vector<Experiment> experiments;
        };

    private:

        /** Path to the batch file */
        std::string path;

        /** Simulations */
        std::vector<Simulate> simulations;

        /** Return the training simulation parsed from the prototxt file
         * @param simulate_proto   prototxt simulation
         * @return Simulate configurations
         */
        Simulate read_training_simulation(const protobuf::Batch_Simulate &simulate_proto);

        /** Return the inference simulation parsed from the prototxt file
         * @param simulate_proto   prototxt simulation
         * @return Simulate configuration
         */
        Simulate read_inference_simulation(const protobuf::Batch_Simulate &simulate_proto);

    public:

        /** Constructor
         * @param _path     Path to the batch file
         */
        explicit Batch(const std::string &_path){ this->path = _path; }

        /** Parse the batch file into memory */
        void read_batch();

        /**
         * Get all simulate configurations
         * @return Array of simulate configurations
         */
        const std::vector<Simulate> &getSimulations() const;

    };

}


#endif //DNNSIM_BATCH_H
