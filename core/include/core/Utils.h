#ifndef DNNSIM_UTILS_H
#define DNNSIM_UTILS_H

#include <base/Layer.h>
#include <base/Network.h>
#include <interface/NetReader.h>
#include <sys/common.h>

namespace core {

    typedef std::vector<std::vector<std::vector<double>>> OutputTensor;

    typedef std::tuple<uint16_t, uint16_t> ValueIndex;

    template <typename T>
    using ValueTuple = std::tuple<T, uint16_t, uint16_t>;

    template <typename T>
    using BufferRow = std::vector<ValueTuple<T>>;

    template <typename T>
    using BufferSet = std::vector<std::vector<ValueTuple<T>>>;

    template <typename T>
    using Buffer = std::vector<std::vector<std::vector<ValueTuple<T>>>>;

    typedef std::tuple<int, int> WindowCoord;

    template <typename T>
    class TileData {
    public:
        BufferSet<T> act_row;
        BufferRow<T> wgt_row;
        std::vector<WindowCoord> windows;
        std::vector<int> filters;
        int time = 0;
        bool valid = false;
    };

    typedef union {
        float f;
        struct {
            unsigned int truncated_mantissa : 16;
            unsigned int mantissa : 7;
            unsigned int exponent : 8;
            unsigned int sign : 1;
        } field;
    } bfloat16;

    /** Read training traces for a given epoch
     * @param network_name      Name of the network
     * @param batch             Batch of the traces
     * @param epoch             Epoch of the traces
     * @param traces_mode       Fordward/Backward traces
     * @param QUIET             Avoid std::out messages
     */
    template <typename T>
    base::Network<T> read_training(const std::string &network_name, uint32_t batch, uint32_t epoch,
            uint32_t traces_mode, bool QUIET);

    /** Iterate set of windows in groups
     * @param out_x         Output activations X size
     * @param out_y         Output activations Y size
     * @param list_x        X position for the set of input windows (Overwritten)
     * @param list_y        Y position for the set of input windows (Overwritten)
     * @param x_counter     X input window counter (Overwritten)
     * @param y_counter     Y input window counter (Overwritten)
     * @param max_windows   Maximum number of windows (Number of columns in the accelerator)
     * @return              Return false when all input windows are read
     */
    bool iterateWindows(long out_x, long out_y, std::vector<int> &list_x, std::vector<int> &list_y,
                        int &x_counter, int &y_counter, int max_windows = 16);

    /** Split sign, exponent, and mantissa in bfloat16 format from a float
     * @param number    Floating point 32 number
     * return           Tuple containing sign, exponent, and mantissa (truncated)
     */
    std::tuple<uint8_t,uint8_t,uint8_t> split_bfloat16(float number);

    /** Return floating-point single precision number in bfloat 16
     * @param number    Floating point 32 number
     * return           BFloat 16 number
     */
    float cast_bfloat16(float number);

    /** Return the optimal encoding for the given value
     * @param value     Value to encode WITHOUT the sign
     * @return          Value with the optimal encoding
     */
    uint16_t booth_encoding(uint16_t value);

    /** Return the minimum and maximum index position for a given value
     * @param value     Value to get the indexes
     * @return          Minimum and maximum indexes
     */
    std::tuple<uint8_t,uint8_t> minMax(uint16_t value);

    /** Return the number of effectual bits for a given value
     * @param value     Value to get the effectual bits
     * @return          Number of effectual bits
     */
    uint8_t effectualBits(uint16_t value);

    /** Return true if all the queues of activation bits are empty
     * @param offsets   Collection of activations with their explicit one positions in a queue
     * @return          True if empty
     */
    bool check_act_bits(const std::vector<std::queue<uint8_t>> &offsets);

    /** Return value into sign-magnitude representation
     * @param two_comp  Signed value in two complement
     * @param mask      Mask with one bit for the bit position
     * @return          Value in sign-magnitude
     */
    uint16_t sign_magnitude(short two_comp, uint16_t mask);

}

#endif //DNNSIM_UTILS_H
