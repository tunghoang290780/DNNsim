
#include <sys/Batch.h>

namespace sys {

    bool ReadProtoFromTextFile(const char* filename, google::protobuf::Message* proto) {
        int fd = open(filename, O_RDONLY);
        auto input = new google::protobuf::io::FileInputStream(fd);
        bool success = google::protobuf::TextFormat::Parse(input, proto);
        delete input;
        close(fd);
        return success;
    }

    Batch::Transform Batch::read_transformation(const protobuf::Batch_Transform &transform_proto) {
        Batch::Transform transform;
        std::string value;
        transform.network = transform_proto.network();

        value = transform_proto.inputtype();
        if(value  != "Caffe" && value != "Protobuf" && value != "Gzip")
            throw std::runtime_error("Input type configuration for network " + value +
                " must be <Caffe|Protobuf|Gzip>.");
        else
            transform.inputType = transform_proto.inputtype();

        value = transform_proto.inputdatatype();
        if(value  != "Float32" && value != "Fixed16")
            throw std::runtime_error("Input data type configuration for network " + value +
                " must be <Float32|Fixed16.");
        else
            transform.inputDataType = transform_proto.inputdatatype();

        value = transform_proto.outputtype();
        if(value != "Protobuf" && value != "Gzip")
            throw std::runtime_error("Output type configuration for network " + value +
                " must be <Caffe|Protobuf|Gzip>.");
        else
            transform.outputType = transform_proto.outputtype();

        value = transform_proto.outputdatatype();
        if(value  != "Float32" && value != "Fixed16")
            throw std::runtime_error("Output data type configuration for network " + value +
                                     " must be <Float32|Fixed16.");
        else {
            // Only allow conversion from float32 to fixed16
            std::string data_conversion = transform_proto.inputdatatype() == "Float32" &&
                                          transform_proto.outputdatatype() == "Fixed16"
                                          ? transform_proto.outputdatatype() : "Not";
            transform.outputDataType = data_conversion;
        }

        return transform;
    }

    Batch::Simulate Batch::read_simulation(const protobuf::Batch_Simulate &simulate_proto) {
        Batch::Simulate simulate;
        std::string value;
        simulate.network = simulate_proto.network();

        value = simulate_proto.inputtype();
        if(value  != "Caffe" && value != "Protobuf" && value != "Gzip")
            throw std::runtime_error("Input type configuration for network " + value +
                                     " must be <Caffe|Protobuf|Gzip>.");
        else
            simulate.inputType = simulate_proto.inputtype();

        value = simulate_proto.inputdatatype();
        if(value  != "Float32" && value != "Fixed16")
            throw std::runtime_error("Input data type configuration for network " + value +
                                     " must be <Float32|Fixed16.");
        else
            simulate.inputDataType = simulate_proto.inputdatatype();

        if (simulate.inputDataType == "Fixed16") {
            for(const auto &experiment_proto : simulate_proto.experiment()) {
                Batch::Simulate::Experiment experiment;
                if(experiment_proto.architecture() == "BitPragmatic") {
                    value = experiment_proto.task();
                    if(value  != "Cycles" && value != "MemAccesses")
                        throw std::runtime_error("BitPragmatic simulation type for network " + value +
                                                 " must be <Cycles|MemAccesses.");
                } else if (experiment_proto.architecture() == "Laconic") {
                    value = experiment_proto.task();
                    if(value  != "Cycles" && value != "Potentials")
                        throw std::runtime_error("Laconic simulation type for network " + value +
                                                 " must be <Cycles|Potentials.");
                } else throw std::runtime_error("Architecture for network " + value +
                                                " in Fixed16 must be <BitPragmatic|Laconic.");
                experiment.architecture = experiment_proto.architecture();
                experiment.task = experiment_proto.task();
                simulate.experiments.emplace_back(experiment);
            }
        }

        return simulate;
    }

    void Batch::read_batch() {
        GOOGLE_PROTOBUF_VERIFY_VERSION;

        protobuf::Batch batch;

        if (!ReadProtoFromTextFile(this->path.c_str(),&batch)) {
            throw std::runtime_error("Failed to read prototxt");
        }

        for(const auto &transform : batch.transform()) {
            try {
                this->transformations.emplace_back(read_transformation(transform));
            } catch (std::exception &exception) {
                std::cerr << "Prototxt transformation error: " << exception.what() << std::endl;
                #ifdef STOP_AFTER_ERROR
                exit(1);
                #endif
            }
        }

        for(const auto &simulate : batch.simulate()) {
            try {
                this->simulations.emplace_back(read_simulation(simulate));
            } catch (std::exception &exception) {
                std::cerr << "Prototxt simulation error: " << exception.what() << std::endl;
                #ifdef STOP_AFTER_ERROR
                exit(1);
                #endif
            }
        }

    }

    /* Getters */
    const std::vector<Batch::Transform> &Batch::getTransformations() const { return transformations; }
    const std::vector<Batch::Simulate> &Batch::getSimulations() const { return simulations; }

}