#include <vip_lite.h>
#include <stdio.h>
#include <memory.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__)
#include <sys/time.h>
#endif

#ifdef __linux__
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#else
#include <stdint.h>
#include <io.h>
#include <direct.h>
#endif

#define CREATE_NETWORK_FROM_MEMORY  1
//#define CREATE_NETWORK_FROM_FLASH   1
//#define CREATE_NETWORK_FROM_FILE    1

#define MAX_DIMENSION_NUMBER    4
#define MAX_INPUT_OUTPUT_NUM    20
#define MATH_ABS(x)      (((x) < 0)    ? -(x) :  (x))
#define MATH_MAX(a,b)    (((a) > (b)) ? (a) : (b))
#define MATH_MIN(a,b)    (((a) < (b)) ? (a) : (b))

#define MAX_SUPPORT_RUN_NETWORK   128
void *network_buffer[MAX_SUPPORT_RUN_NETWORK] = {VIP_NULL};

/* align up */
#define GCVIP_ALIGN(n, align)              \
(                                          \
    ((n) + ((align) - 1)) & ~((align) - 1) \
)

#define BITS_PER_BYTE 8

const char *usage =
    "vpm_run -s sample.txt -l loop_run_count -d device_index\n"
    "-s sample.txt:     to include one ore more network binary graph (NBG) data file resource.\n"
    "                   See sample.txt for details.\n"
    "-l loop_run_count: the number of loop run network.\n"
    "-d device_index:   specify this NBG runs device.\n"
    "-t time_out:       specify milliseconds time out of network.\n"
    "-b bypass_level:   set value 1 to bypass saving output txt/binary file and showing top5.\n"
    "--show_top5 level: set value 1 to show top5 when bypass_level is 0.\n"
    "--save_txt level:  set value 1 to save txt output when bypass_level is 0.\n"
#if NPU_SW_VERSION >= 2
    "-c core_index:     specify this start core index of device.\n"
    "--layer_profile_dump:      set value 1 to enable NPD function.\n"
    "--preload:         set value 1 to enable preload coeff into vipsram.\n"
    "--op_segment:      set which operations will be run. example: --op_segment 10,20 means run 10 ~ 20\n"
    "--layer_dump:  layer dump. eg: --layer_dump -1 dump all layer, --layer_dump 19, --layer_dump 18,20\n"
#endif
    "-h : help\n"
    "example: ./vpm_run -s sample.txt -l 10 -d 1 specify the NBG runs 10 times on device 1.\n";


typedef struct _vpm_network_param {
    vip_uint8_t     enable_npd;
    vip_uint8_t     preload_vipsram;
    vip_uint32_t    time_out;
    vip_uint32_t    start_op;
    vip_uint32_t    end_op;
    vip_uint32_t    device_index;
    vip_uint32_t    core_index;
} vpm_network_param_t;

typedef struct _vpm_network_task {
    /* task information. */
    char          **base_strings;
    int             string_count;
    int             nbg_name;
    int             input_count;
    int            *input_names;
    int             output_count;
    int            *output_names;
    int             golden_count;
    int            *golden_names;
    void           **golden_data;
    vip_uint64_t   *golden_size;

    /* VIP lite buffer objects. */
    vip_network     network;
    vip_buffer     *input_buffers;
    vip_buffer     *output_buffers;

    vip_uint32_t   loop_count;
    vip_uint32_t   infer_cycle;
    vip_uint32_t   infer_time;
    vip_uint64_t   total_infer_cycle;
    vip_uint64_t   total_infer_time;
    vip_uint32_t   core_count;

    vpm_network_param_t param;
} vpm_network_task_t;

typedef enum _file_type_e
{
    NN_FILE_NONE = 0,
    NN_FILE_TENSOR = 1,
    NN_FILE_BINARY = 2,
    NN_FILE_TEXT = 3,
    NN_FILE_MAX
} file_type_e;

/*
 * A helper union for fp32 bit casting.
 */
typedef union {
    float val;
    vip_uint32_t data;
} fp32_bit_cast_t;

typedef union
{
    unsigned int u;
    float f;
} _fp32_t;


#if defined(__linux__)
#define TIME_SLOTS   10
vip_uint64_t time_begin[TIME_SLOTS];
vip_uint64_t time_end[TIME_SLOTS];
static vip_uint64_t GetTime(void)
{
    struct timeval time;
    gettimeofday(&time, NULL);
    return (vip_uint64_t)(time.tv_usec + time.tv_sec * 1000000);
}

static void TimeBegin(int id)
{
    time_begin[id] = GetTime();
}

static void TimeEnd(int id)
{
    time_end[id] = GetTime();
}

static vip_uint64_t TimeGet(int id)
{
    return time_end[id] - time_begin[id];
}
#endif

vip_status_e vip_memset(vip_uint8_t *dst, vip_uint32_t size)
{
    vip_status_e status = VIP_SUCCESS;
#if 0
    vip_uint32_t i = 0;
    for (i = 0; i < size; i++) {
        dst[i] = 0;
    }
#else
    memset(dst, 0, size);
#endif
    return status;
}

vip_status_e vip_memcpy(vip_uint8_t *dst, vip_uint8_t *src, vip_uint32_t size)
{
    vip_status_e status = VIP_SUCCESS;
#if 0
    vip_uint32_t i = 0;
    for (i = 0; i < size; i++) {
        dst[i] = src[i];
    }
#else
    memcpy(dst, src, size);
#endif
    return status;
}

typedef struct
{
    vip_uint8_t* raw_addr;
} aligned_header;

vip_uint8_t * vsi_nn_MallocAlignedBuffer
    (
    vip_uint32_t mem_size,
    vip_uint32_t align_start_size,
    vip_uint32_t align_block_size
    )
{
    vip_uint32_t sz;
    long temp;
    vip_uint8_t* raw_addr;
    vip_uint8_t* p;
    vip_uint8_t* align_addr;
    aligned_header* header;

    sz = sizeof(aligned_header) + mem_size + align_start_size + align_block_size;
    raw_addr = (vip_uint8_t *)malloc(sz * sizeof(vip_uint8_t ) );
    memset(raw_addr, 0, sizeof(vip_uint8_t ) * sz);
    p = raw_addr + sizeof(aligned_header);

    temp = (long)(p) % align_start_size;
    if (temp == 0)
    {
        align_addr = p;
    }
    else
    {
        align_addr = p + align_start_size - temp;
    }
    header = (aligned_header*)(align_addr - sizeof(aligned_header));
    header->raw_addr = raw_addr;

    return align_addr;
}/* vsi_nn_MallocAlignedBuffer() */

void vsi_nn_FreeAlignedBuffer
    (
    vip_uint8_t* handle
    )
{
    aligned_header* header;
    header = (aligned_header*)(handle - sizeof(aligned_header));
    free(header->raw_addr);
}

unsigned int load_file(const char *name, void *dst)
{
    FILE *fp = fopen(name, "rb");
    unsigned int size = 0;

    if (fp != NULL) {
        fseek(fp, 0, SEEK_END);
        size = ftell(fp);

        fseek(fp, 0, SEEK_SET);
        size = fread(dst, size, 1, fp);

        fclose(fp);
    }

    return size;
}

unsigned int save_file(const char *name, void *data, unsigned int size)
{
    FILE *fp = fopen(name, "wb+");
    unsigned int saved = 0;

    if (fp != NULL) {
        saved = fwrite(data, size, 1, fp);

        fclose(fp);
    }
    else {
        printf("Saving file %s failed.\n", name);
    }

    return saved;
}

vip_uint64_t get_file_size(const char *name)
{
    FILE    *fp = fopen(name, "rb");
    vip_uint64_t size = 0;

    if (fp != NULL) {
        fseek(fp, 0, SEEK_END);
        size = ftell(fp);

        fclose(fp);
    }
    else {
        printf("Checking file %s failed.\n", name);
        size = 0;
    }

    return size;
}

int get_file_type(const char *file_name)
{
    int type =NN_FILE_NONE;
    const char *ptr;
    char sep = '.';
    unsigned int pos,n;
    char buff[32] = {0};

    ptr = strrchr(file_name, sep);
    pos = ptr - file_name;
    n = strlen(file_name) - (pos + 1);
    strncpy(buff, file_name+(pos+1), n);

    if (strcmp(buff, "tensor") == 0) {
        type = NN_FILE_TENSOR;
    }
    else if(strcmp(buff, "dat") == 0 || !strcmp(buff, "bin")) {
        type = NN_FILE_BINARY;
    }
    else if(strcmp(buff, "txt") == 0) {
        type = NN_FILE_TEXT;
    }
    else {
        printf("unsupported input file type=%s.\n", buff);
    }

    return type;
}

vip_uint32_t type_get_bytes(const vip_enum type)
{
    switch(type)
    {
        case VIP_BUFFER_FORMAT_INT8:
        case VIP_BUFFER_FORMAT_UINT8:
#if NPU_SW_VERSION >= 2
        case VIP_BUFFER_FORMAT_BOOL8:
#endif
            return 1;
        case VIP_BUFFER_FORMAT_INT16:
        case VIP_BUFFER_FORMAT_UINT16:
        case VIP_BUFFER_FORMAT_FP16:
        case VIP_BUFFER_FORMAT_BFP16:
            return 2;
        case VIP_BUFFER_FORMAT_FP32:
        case VIP_BUFFER_FORMAT_INT32:
        case VIP_BUFFER_FORMAT_UINT32:
            return 4;
        case VIP_BUFFER_FORMAT_FP64:
        case VIP_BUFFER_FORMAT_INT64:
        case VIP_BUFFER_FORMAT_UINT64:
            return 8;
        case VIP_BUFFER_FORMAT_INT4:
        case VIP_BUFFER_FORMAT_UINT4:
            return 1;

        default:
            return 0;
    }
}

vip_uint32_t type_get_bits(const vip_enum type)
{
    switch(type)
    {
        case VIP_BUFFER_FORMAT_INT8:
        case VIP_BUFFER_FORMAT_UINT8:
#if NPU_SW_VERSION >= 2
        case VIP_BUFFER_FORMAT_BOOL8:
#endif
            return 1 * BITS_PER_BYTE;
        case VIP_BUFFER_FORMAT_INT16:
        case VIP_BUFFER_FORMAT_UINT16:
        case VIP_BUFFER_FORMAT_FP16:
        case VIP_BUFFER_FORMAT_BFP16:
            return 2 * BITS_PER_BYTE;
        case VIP_BUFFER_FORMAT_FP32:
        case VIP_BUFFER_FORMAT_INT32:
        case VIP_BUFFER_FORMAT_UINT32:
            return 4 * BITS_PER_BYTE;
        case VIP_BUFFER_FORMAT_FP64:
        case VIP_BUFFER_FORMAT_INT64:
        case VIP_BUFFER_FORMAT_UINT64:
            return 8 * BITS_PER_BYTE;
        case VIP_BUFFER_FORMAT_INT4:
        case VIP_BUFFER_FORMAT_UINT4:
            return BITS_PER_BYTE / 2;

        default:
            return 0;
    }
}

 vip_uint32_t get_tensor_size(
    vip_int32_t *shape,
    vip_uint32_t dim_num,
    vip_enum type
    )
{
    vip_uint32_t sz;
    vip_uint32_t i;
    sz = 0;
    if(NULL == shape || 0 == dim_num)
    {
        return sz;
    }
    sz = type_get_bits(type);
    for(i = 0; i < dim_num; i ++)
    {
        sz *= shape[i];
        if (0 == i) {
            /* round up */
            sz = (sz + BITS_PER_BYTE - 1) / BITS_PER_BYTE;
        }
    }

    return sz;
}

vip_uint32_t get_element_num(
    vip_int32_t *sizes,
    vip_uint32_t num_of_dims,
    vip_enum data_format
    )
{
    vip_uint32_t num = 1;
    vip_uint32_t i = 0;

    for (i = 0; i < num_of_dims; i++) {
        num *= sizes[i];
    }

    return num;
}

vip_int32_t type_is_integer(const vip_enum type)
{
    vip_int32_t ret;
    ret = 0;
    switch(type)
    {
    case VIP_BUFFER_FORMAT_INT8:
    case VIP_BUFFER_FORMAT_INT16:
    case VIP_BUFFER_FORMAT_INT32:
    case VIP_BUFFER_FORMAT_UINT8:
    case VIP_BUFFER_FORMAT_UINT16:
    case VIP_BUFFER_FORMAT_UINT32:
    case VIP_BUFFER_FORMAT_UINT4:
    case VIP_BUFFER_FORMAT_INT4:
#if NPU_SW_VERSION >= 2
    case VIP_BUFFER_FORMAT_BOOL8:
#endif
        ret = 1;
        break;
    default:
        break;
    }

    return ret;
}

vip_int32_t type_is_signed(const vip_enum type)
{
    vip_int32_t ret;
    ret = 0;
    switch(type)
    {
    case VIP_BUFFER_FORMAT_INT4:
    case VIP_BUFFER_FORMAT_INT8:
    case VIP_BUFFER_FORMAT_INT16:
    case VIP_BUFFER_FORMAT_INT32:
    case VIP_BUFFER_FORMAT_INT64:
    case VIP_BUFFER_FORMAT_BFP16:
    case VIP_BUFFER_FORMAT_FP16:
    case VIP_BUFFER_FORMAT_FP32:
    case VIP_BUFFER_FORMAT_FP64:
        ret = 1;
        break;
    default:
        break;
    }

    return ret;
}

void type_get_range(vip_enum type, double *max_range, double * min_range)
{
    vip_int32_t bits;
    double from, to;
    from = 0.0;
    to = 0.0;
    bits = type_get_bits(type);
    if(type_is_integer(type)) {
        if(type_is_signed(type)) {
            from = (double)(-(1L << (bits - 1)));
            to = (double)((1UL << (bits - 1)) - 1);
        }
        else {
            from = 0.0;
            to = (double)((1UL << bits) - 1);
        }
    }
    else {
        //  TODO: Add float
    }
    if(NULL != max_range) {
        *max_range = to;
    }
    if(NULL != min_range) {
        *min_range = from;
    }
}

double copy_sign(double number, double sign)
{
    double value = MATH_ABS(number);
    return (sign > 0) ? value : (-value);
}

int math_floorf(double x)
{
    if (x >= 0)
    {
        return (int)x;
    }
    else
    {
        return (int)x - 1;
    }
}

double rint(double x)
{
#define _EPSILON 1e-8
    double decimal;
    double inter;
    int intpart;

    intpart = (int)x;
    decimal = x - intpart;
    inter = (double)intpart;

    if(MATH_ABS((MATH_ABS(decimal) - 0.5f)) < _EPSILON )
    {
        inter += (vip_int32_t)(inter) % 2;
    }
    else
    {
        return copy_sign(math_floorf(MATH_ABS(x) + 0.5f), x);
    }

    return inter;
}

vip_int32_t fp32_to_dfp(const float in,  const signed char fl, const vip_enum type)
{
    vip_int32_t data;
    double max_range;
    double min_range;
    type_get_range(type, &max_range, &min_range);
    if(fl > 0 )
    {
        data = (vip_int32_t)rint(in * (float)(1 << fl ));
    }
    else
    {
        data = (vip_int32_t)rint(in * (1.0f / (float)(1 << -fl )));
    }
    data = MATH_MIN(data, (vip_int32_t)max_range);
    data = MATH_MAX(data, (vip_int32_t)min_range);

    return data;
}

static float dfp_to_fp32(
    const int32_t val,
    const int8_t  fl,
    const vip_enum type
    )
{
    float result;
    if(fl > 0 ) {
        result = (float)val * (1.0f / ((float) ((int64_t)1 << fl ) ) );
    }
    else
    {
        result = (float)val * ((float) ((int64_t)1 << -fl ) );
    }
    return result;
}

vip_int32_t fp32_to_affine(
    const float in,
    const float scale,
    const  int zero_point,
    const vip_enum type
    )
{
    vip_int32_t data;
    double max_range;
    double min_range;
    type_get_range(type, &max_range, &min_range);
    data = (vip_int32_t)(rint(in / scale ) + zero_point);
    data = MATH_MAX((vip_int32_t)min_range, MATH_MIN((vip_int32_t)max_range , data ));
    return data;
}

static vip_status_e integer_convert(
    const void * src,
    void *dest,
    vip_enum src_type,
    vip_enum dest_type
    )
{
    vip_status_e status = VIP_SUCCESS;
    if (type_is_integer(src_type ) && type_is_integer(dest_type ) ) {
        vip_uint8_t all_zeros[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
        vip_uint8_t all_ones[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
        vip_uint32_t src_sz = type_get_bytes(src_type );
        vip_uint32_t dest_sz = type_get_bytes(dest_type );
        vip_uint8_t* buffer = all_zeros;
        if(src_sz == 0 ) {
            src_sz = 1;
        }
        if(dest_sz == 0) {
            dest_sz = 1;
        }
        if(type_is_signed(src_type ) && (((int8_t *)src)[src_sz - 1] & 0x80) ) {
            buffer = all_ones;
        }
        memcpy(buffer, src, src_sz );
        memcpy(dest, buffer, dest_sz );
    }
    else {
        status = VIP_ERROR_FAILURE;
    }
    return status;
}

static unsigned short  fp32_to_bfp16_rtne(float in)
{
    /*
    Convert a float point to bfloat16, with round-nearest-to-even as rounding method.
    */
    vip_uint32_t fp32 = *((unsigned int *) &in);
    unsigned short  out;

    vip_uint32_t lsb = (fp32 >> 16) & 1;    /* Least significant bit of resulting bfloat. */
    vip_uint32_t rounding_bias = 0x7fff + lsb;

    if (0x7FC00000 == in ) {
        out = 0x7fc0;
    }
    else {
        fp32 += rounding_bias;
        out = (unsigned short ) (fp32 >> 16);
    }

    return out;
}

static float bfp16_to_fp32(vip_int16_t in)
{
    vip_uint32_t t1, t2, t3;
    float out;
    fp32_bit_cast_t fp32_bit_cast;

    t1 = in & 0x00FF;                       // Mantissa
    t2 = in & 0xFF00;                       // Sign bit + Exponent
    t3 = in & 0x7F00;                       // Exponent

    t1 <<= 16;
    t2 <<= 16;                              // Shift (sign + Exponent) bit into position
    t1 |= t2;                               // Re-insert (sign + Exponent) bit

    fp32_bit_cast.data = t1;
    out = fp32_bit_cast.val;

    return t3 == 0 ? 0.0f : out;
}

unsigned short fp32_to_fp16(float in)
{
    vip_uint32_t fp32 = 0;
    vip_uint32_t t1 = 0;
    vip_uint32_t t2 = 0;
    vip_uint32_t t3 = 0;
    vip_uint32_t fp16 = 0u;

    vip_memcpy((vip_uint8_t*)&fp32, (vip_uint8_t*)&in, sizeof(vip_uint32_t));

    t1 = (fp32 & 0x80000000u) >> 16;  /* sign bit. */
    t2 = (fp32 & 0x7F800000u) >> 13;  /* Exponent bits */
    t3 = (fp32 & 0x007FE000u) >> 13;  /* Mantissa bits, no rounding */

    if(t2 >= 0x023c00u )
    {
        fp16 = t1 | 0x7BFF;     /* Don't round to infinity. */
    }
    else if(t2 <= 0x01c000u )
    {
        fp16 = t1;
    }
    else
    {
        t2 -= 0x01c000u;
        fp16 = t1 | t2 | t3;
    }

    return (unsigned short) fp16;
}

static float fp16_to_fp32(const short in)
{
    const _fp32_t magic = { (254 - 15) << 23 };
    const _fp32_t infnan = { (127 + 16) << 23 };
    _fp32_t o;
    // Non-sign bits
    o.u = (in & 0x7fff ) << 13;
    o.f *= magic.f;
    if(o.f  >= infnan.f)
    {
        o.u |= 255 << 23;
    }
    //Sign bit
    o.u |= (in & 0x8000 ) << 16;
    return o.f;
}

static vip_float_t affine_to_fp32(vip_int32_t val, vip_int32_t zeroPoint, vip_float_t scale)
{
    vip_float_t result = 0.0f;
    result = ((vip_float_t)val - zeroPoint) * scale;
    return result;
}

vip_status_e float32_to_dtype(
    float src,
    unsigned char *dst,
    const vip_enum data_type,
    const vip_enum quant_format,
    signed char fixed_point_pos,
    float tf_scale,
    vip_int32_t tf_zerop
    )
{
    vip_status_e status = VIP_SUCCESS;

    switch(data_type )
    {
    case VIP_BUFFER_FORMAT_FP32:
        *(float *)dst = src;
        break;
    case VIP_BUFFER_FORMAT_FP16:
        *(vip_int16_t *)dst = fp32_to_fp16(src);
        break;
    case VIP_BUFFER_FORMAT_BFP16:
        *(vip_int16_t *)dst = fp32_to_bfp16_rtne(src);
        break;
    case VIP_BUFFER_FORMAT_INT8:
    case VIP_BUFFER_FORMAT_UINT8:
#if NPU_SW_VERSION >= 2
    case VIP_BUFFER_FORMAT_BOOL8:
#endif
    case VIP_BUFFER_FORMAT_INT16:
    case VIP_BUFFER_FORMAT_UINT16:
    case VIP_BUFFER_FORMAT_INT32:
    case VIP_BUFFER_FORMAT_UINT32:
    case VIP_BUFFER_FORMAT_INT4:
    case VIP_BUFFER_FORMAT_UINT4:
        {
            vip_int32_t dst_value = 0;
            switch(quant_format)
            {
            case VIP_BUFFER_QUANTIZE_DYNAMIC_FIXED_POINT:
                dst_value = fp32_to_dfp(src, fixed_point_pos, data_type);
                break;
            case VIP_BUFFER_QUANTIZE_TF_ASYMM:
                dst_value = fp32_to_affine(src, tf_scale, tf_zerop, data_type);
                break;
            case VIP_BUFFER_QUANTIZE_NONE:
                dst_value = (vip_int32_t)src;
                break;
            default:
                break;
            }
            integer_convert(&dst_value, dst, VIP_BUFFER_FORMAT_INT32, data_type);
        }
        break;
    default:
        printf("unsupported tensor type\n");;
    }

    return status;
}

static vip_status_e dtype_to_float32(
    vip_uint8_t *src,
    float *dst,
    const vip_enum src_dtype,
    const vip_enum quant_format,
    signed char fixed_point_pos,
    float tf_scale,
    vip_int32_t tf_zerop
    )
{
    switch(src_dtype)
    {
    case VIP_BUFFER_FORMAT_FP32:
        *dst = *(float *)src;
        break;
    case VIP_BUFFER_FORMAT_FP16:
        *dst = fp16_to_fp32(*(vip_int16_t *)src);
        break;
    case VIP_BUFFER_FORMAT_BFP16:
        *dst = bfp16_to_fp32(*(vip_int16_t *)src);
        break;
    case VIP_BUFFER_FORMAT_INT4:
    case VIP_BUFFER_FORMAT_UINT4:
    case VIP_BUFFER_FORMAT_INT8:
#if NPU_SW_VERSION >= 2
    case VIP_BUFFER_FORMAT_BOOL8:
#endif
    case VIP_BUFFER_FORMAT_UINT8:
    case VIP_BUFFER_FORMAT_INT16:
    case VIP_BUFFER_FORMAT_UINT16:
    case VIP_BUFFER_FORMAT_INT32:
        {
            int32_t src_value = 0;
            integer_convert(src, &src_value, src_dtype, VIP_BUFFER_FORMAT_INT32);
            switch(quant_format)
            {
            case VIP_BUFFER_QUANTIZE_DYNAMIC_FIXED_POINT:
                *dst = dfp_to_fp32(src_value, fixed_point_pos, src_dtype);
                break;
            case VIP_BUFFER_QUANTIZE_TF_ASYMM:
                *dst = affine_to_fp32(src_value, tf_zerop, tf_scale);
                break;
            case VIP_BUFFER_QUANTIZE_NONE:
                *dst = (float)src_value;
                break;
            default:
                break;
            }
        }
        break;
    default:
        return VIP_ERROR_FAILURE;
    }

    return VIP_SUCCESS;
}

unsigned char *get_binary_data(
    char *file_name,
    vip_uint64_t *file_size
    )
{
    unsigned char *tensorData;

    *file_size = get_file_size((const char *)file_name);
    if (0 == *file_size) {
        return VIP_NULL;
    }
    tensorData = (unsigned char *)malloc(*file_size * sizeof(unsigned char));
    load_file(file_name, (void *)tensorData);

    return tensorData;
}

static vip_bool_e compare_low_4bits(
    vip_uint8_t* src0,
    vip_uint8_t* src1
    )
{
    vip_bool_e is_equal = vip_false_e;
    vip_uint8_t src0_low = 0, src1_low = 0;

    src0_low = src0[0] & 0x0F;
    src1_low = src1[0] & 0x0F;

    if (src0_low == src1_low) {
        is_equal = vip_true_e;
    }

    return is_equal;
}

vip_status_e pack_4bit_data(
    vip_uint8_t* src,
    vip_uint8_t* dest,
    vip_uint32_t size,
    vip_uint32_t x_dim
    )
{
    vip_status_e status = VIP_SUCCESS;
    vip_uint32_t i = 0, j = 0;
    vip_uint8_t high = 0, low = 0;
    for (i = 0; i < size; i++)
    {
        if ((i + 1) % x_dim == 0)
        {
            high = 0;
            low = src[i];
        }
        else
        {
            high = src[i + 1];
            low = src[i];
            i++;
        }
        dest[j] = (high << 4) | (low & 0xF);
        j++;
    }

    return status;
}

vip_status_e unpack_4bit_data(
    vip_uint8_t* src,
    vip_uint8_t* dest,
    vip_uint32_t size,
    vip_uint32_t x_dim,
    vip_bool_e sign
    )
{
    vip_status_e status = VIP_SUCCESS;
    vip_uint32_t i = 0, j = 0;
    vip_uint8_t high = 0, low = 0;
    for (i = 0; i < size; i++)
    {
        high = src[i] >> 4;
        low = src[i] & 0x0F;
        if (vip_true_e == sign) {
            if (high > 7)
            {
                high = high | 0xF0;
            }
            if (low > 7)
            {
                low = low | 0xF0;
            }
        }

        if ((j + 1) % x_dim == 0)
        {
            dest[j] = low;
            j++;
        }
        else
        {
            dest[j] = low;
            dest[j + 1] = high;
            j += 2;
        }
    }

    return status;
}

unsigned char *get_tensor_data(
    vpm_network_task_t *task,
    char *file_name,
    vip_uint64_t *file_size,
    vip_uint32_t index
    )
{
    vip_uint32_t sz = 1;
    vip_uint32_t stride = 1;
    vip_int32_t sizes[4];
    vip_uint32_t num_of_dims;
    vip_uint32_t i = 0;
    vip_enum data_format;
    vip_enum quant_format;
    vip_int32_t fixed_point_pos;
    float tf_scale;
    vip_int32_t tf_zerop;
    unsigned char *tensorData = NULL;
    FILE *tensorFile;
    float fval = 0.0;

    tensorFile = fopen(file_name, "rb");

    vip_query_input(task->network, index, VIP_BUFFER_PROP_NUM_OF_DIMENSION, &num_of_dims);
    vip_query_input(task->network, index, VIP_BUFFER_PROP_DATA_FORMAT, &data_format);
    vip_query_input(task->network, index, VIP_BUFFER_PROP_QUANT_FORMAT, &quant_format);
    vip_query_input(task->network, index, VIP_BUFFER_PROP_FIXED_POINT_POS, &fixed_point_pos);
    vip_query_input(task->network, index, VIP_BUFFER_PROP_TF_SCALE, &tf_scale);
    vip_query_input(task->network, index, VIP_BUFFER_PROP_SIZES_OF_DIMENSION, sizes);
    vip_query_input(task->network, index, VIP_BUFFER_PROP_TF_ZERO_POINT, &tf_zerop);

    sz = get_element_num(sizes, num_of_dims, data_format);
    stride = type_get_bytes(data_format);
    tensorData = (unsigned char *)malloc(stride * sz * sizeof(unsigned char));
    memset(tensorData, 0, stride * sz * sizeof(unsigned char));
    *file_size = stride * sz * sizeof(unsigned char);

    for (i = 0; i < sz; i++) {
        fscanf(tensorFile, "%f ", &fval);
        float32_to_dtype(fval, &tensorData[stride * i], data_format, quant_format,
                         fixed_point_pos, tf_scale, tf_zerop);
    }

    fclose(tensorFile);

    if (VIP_BUFFER_FORMAT_INT4 == data_format || VIP_BUFFER_FORMAT_UINT4 == data_format) {
        vip_uint32_t output_size = type_get_bits(data_format);
        vip_uint32_t output_element = 1;
        void* pack_data = VIP_NULL;
        for (i = 0; i < num_of_dims; i++) {
            output_element *= sizes[i];
            output_size *= sizes[i];
            if (0 == i) {
                /* round up */
                output_size = (output_size + BITS_PER_BYTE - 1) / BITS_PER_BYTE;
            }
        }
        pack_data = malloc(output_size);
        memset(pack_data, 0, output_size);
        pack_4bit_data(tensorData, pack_data, output_element, sizes[0]);
        free(tensorData);
        tensorData = pack_data;
    }

    return tensorData;
}

void destroy_network(vpm_network_task_t *task)
{
    int i = 0;

    if (task == VIP_NULL) {
        printf("failed task is NULL\n");
        return;
    }

    vip_destroy_network(task->network);

    if (VIP_NULL != task->input_buffers) {
        for (i = 0; i < task->input_count; i++) {
            vip_destroy_buffer(task->input_buffers[i]);
        }
        free(task->input_buffers);
    }

    if (VIP_NULL != task->output_buffers) {
        for (i = 0; i < task->output_count; i++) {
            vip_destroy_buffer(task->output_buffers[i]);
        }
        free(task->output_buffers);
        task->output_buffers = VIP_NULL;
    }
}

void destroy_test_resources(vpm_network_task_t *taskes, vip_int32_t task_count)
{
    vip_int32_t i = 0, j = 0;
    vpm_network_task_t *task = VIP_NULL;

    if (taskes == VIP_NULL) {
        printf("failed task is NULL\n");
        return;
    }

    printf("destroy test resource task_count=%d\n", task_count);

    for (j = 0; j < task_count; j++) {
        task = &taskes[j];

        if (task != VIP_NULL) {
            if (task->input_names != VIP_NULL) {
                free(task->input_names);
                task->input_names = VIP_NULL;
            }
            if (task->output_names != VIP_NULL) {
                free(task->output_names);
                task->output_names = VIP_NULL;
            }
            if (task->golden_names != VIP_NULL) {
                free(task->golden_names);
                task->golden_names = VIP_NULL;
            }
        }
        else {
            printf("failed to destroy task=%d\n", j);
        }
    }

    for (i = 0; i < taskes->string_count; i++) {
        if (taskes->base_strings[i] != VIP_NULL) {
            free(taskes->base_strings[i]);
            taskes->base_strings[i] = VIP_NULL;
        }
    }

    if (taskes->base_strings != VIP_NULL) {
        free (taskes->base_strings);
        taskes->base_strings = VIP_NULL;
    }

    free (taskes);
}

vpm_network_task_t *parse_sample_txt_file(const char *file_name, int *Count)
{
    static const char *tokens[] = {
        "[network]",
        "[input]",
        "[golden]",
        "[output]"
    };
    vpm_network_task_t *task = VIP_NULL, *cur_task = VIP_NULL;
    char line_buffer[255] = {0};
    char *line_string = VIP_NULL;
    int  line_count = 0;
    int  line_len = 0;
    int  network_count = 0;
    int  i;
    int  current_data = 0;
    int  first_task = 1;

    /* Load the task file as a string buffer. */
    FILE *fp = fopen(file_name, "r");
    if (!fp) {
        printf("failed to open file=%s\n", file_name);
        return VIP_NULL;
    }

    /* Count the networks. */
    while (fgets(line_buffer, sizeof(line_buffer), fp) > 0) {
        line_count++;
        if ((line_buffer[strlen(line_buffer) - 1] == '\r') ||
            #if defined(_WIN32)
            (line_buffer[strlen(line_buffer) - 2] == '\r\n') ||
            #endif
            (line_buffer[strlen(line_buffer) - 1] == '\n')) {
            line_buffer[strlen(line_buffer) - 1] = '\0';
        }

        else {
            line_buffer[strlen(line_buffer)] = '\0';
        }
        if(strcmp(line_buffer, tokens[0]) == 0) {
            network_count++;
        }
    }
    *Count = network_count;
    printf("config file read network count=%d\n", network_count);

    /* Allocate taskes. */
    task = (vpm_network_task_t *)malloc(sizeof(vpm_network_task_t) * network_count);
    vip_memset((void*)task, sizeof(vpm_network_task_t)  * network_count);
    if (task == VIP_NULL) {
        return VIP_NULL;
    }

    /* Setup task: set up the strings. */
    task->base_strings = (char **)malloc(sizeof(char *) * line_count);
    task->string_count = line_count;
    fseek(fp, 0, SEEK_SET);
    line_count = 0;
    cur_task = task;

    /* Setup base string. */
    memset(line_buffer, 0, sizeof(line_buffer));
    while (fgets(line_buffer, sizeof(line_buffer), fp) > 0) {
        line_len = strlen(line_buffer);
        cur_task->base_strings[line_count] = (char *)malloc(line_len + 2);
        memset(cur_task->base_strings[line_count], 0, line_len + 1);
        strcpy(cur_task->base_strings[line_count], line_buffer);
        if (cur_task->base_strings[line_count][line_len - 1] == '\n' ||
            #if defined(_WIN32)
            (line_buffer[strlen(line_buffer) - 2] == '\r\n') ||
            #endif
            cur_task->base_strings[line_count][line_len - 1] == '\r') {
            cur_task->base_strings[line_count][line_len - 1] = '\0';
        }
        else {
            /* append \0 for end string */
            cur_task->base_strings[line_count][line_len] = '\0';
        }

        line_count++;
        memset(line_buffer, 0, sizeof(line_buffer));
    }
    fclose(fp);

    /* Locate the nbg strings. */
    cur_task = task;
    cur_task->output_names = NULL;   /* Output name is optional. */
    cur_task->output_count = 0;
    cur_task->input_count  = 0;
    cur_task->golden_count = 0;
    cur_task->output_buffers = NULL;
    cur_task->input_buffers = NULL;
    cur_task->input_names = NULL;
    cur_task->golden_names = NULL;

    for (i = 0; i < line_count; i++) {
        line_string = task->base_strings[i];
        /* Parse the string data. */
        if (line_string[0] == '#') {
            continue;
        }
        else if (line_string[0] == '[') {
            if (strcmp(line_string, tokens[0]) == 0) {
                current_data = 1;
                if (first_task == 0) {
                    cur_task++;
                    cur_task->base_strings = task->base_strings;
                    cur_task->output_count = 0;
                    cur_task->input_count  = 0;
                    cur_task->golden_count = 0;
                    cur_task->output_buffers = NULL;
                    cur_task->input_buffers = NULL;
                    cur_task->output_names = NULL;
                    cur_task->input_names = NULL;
                    cur_task->golden_names = NULL;
                }
                else {
                    first_task = 0;
                }
            }
            else if (strcmp(line_string, tokens[1]) == 0) {
                current_data = 2;
            }
            else if (strcmp(line_string, tokens[2]) == 0) {
                current_data = 3;
            }
            else if (strcmp(line_string, tokens[3]) == 0) {
                current_data = 4;
            }
            else {
                printf("Bad task file. Wrong line @ %d.\n", i);
                free(task->base_strings);
                free(task);
                task = VIP_NULL;
                break;
            }
        }
        else{
            switch (current_data) {
            case 1: /* Network */
                cur_task->nbg_name = i;
                break;

            case 2: /* Input */
                /* Count how many inputs and assign it accordingly. */
                {
                    int iCount = 0;
                    int j;
                    for (j = i; ; j++) {
                        if (j >= line_count)
                            break;

                        if ((task->base_strings[j][0] == '#') ||
                            (task->base_strings[j][0] == '\0'))
                            continue;

                        if (task->base_strings[j][0] != '[') {
                            iCount++;
                        }
                        else {
                            break;
                        }
                    }

                    cur_task->input_count = iCount;
                    cur_task->input_names = (int *)malloc(sizeof(int) * iCount);

                    iCount = 0;
                    for (; ; i++) {
                        if (i >= line_count)
                            break;

                        if ((task->base_strings[i][0] == '#') ||
                            (task->base_strings[i][0] == '\0'))
                            continue;

                        if (task->base_strings[i][0] != '[') {
                            cur_task->input_names[iCount++] = i;
                        }
                        else {
                            i--;
                            break;
                        }
                    }
                }
                break;

            case 3: /* Golden */
                {
                    int iCount = 0;
                    int j;
                    for (j = i; ; j++) {
                        if (j >= line_count)
                            break;

                        if ((task->base_strings[j][0] == '#') ||
                            (task->base_strings[j][0] == '\0'))
                            continue;

                        if (task->base_strings[j][0] != '[') {
                            iCount++;
                        }
                        else {
                            break;
                        }
                    }

                    cur_task->golden_count = iCount;
                    cur_task->golden_names = (int *)malloc(sizeof(int) * iCount);

                    iCount = 0;
                    for (; ; i++) {
                        if (i >= line_count)
                            break;

                        if ((task->base_strings[i][0] == '#') ||
                            (task->base_strings[i][0] == '\0'))
                            continue;

                        if (task->base_strings[i][0] != '[') {
                            cur_task->golden_names[iCount++] = i;
                        }
                        else {
                            i--;
                            break;
                        }
                    }
                }
                break;

            case 4: /* Output */
                {
                    int iCount = 0;
                    int j;
                    for (j = i; ; j++) {
                        if (j >= line_count)
                            break;

                        if ((task->base_strings[j][0] == '#') ||
                            (task->base_strings[j][0] == '\0'))
                            continue;

                        if (task->base_strings[j][0] != '[') {
                            iCount++;
                        }
                        else {
                            break;
                        }
                    }

                    cur_task->output_count = iCount;
                    cur_task->output_names = (int *)malloc(sizeof(int) * iCount);

                    iCount = 0;
                    for (; ; i++) {
                        if (i >= line_count)
                            break;

                        if ((task->base_strings[i][0] == '#') ||
                            (task->base_strings[i][0] == '\0'))
                            continue;

                        if (task->base_strings[i][0] != '[') {
                            cur_task->output_names[iCount++] = i;
                        }
                        else {
                            i--;
                            break;
                        }
                    }
                }
                break;

            default:
                break;
            }
        }
    }

    return task;
}

vip_int32_t init_test_resources(vpm_network_task_t **taskes, const char *taskFileName, int *Count)
{
    vpm_network_task_t *items = VIP_NULL;
    vip_int32_t ret = 0;

    items = parse_sample_txt_file(taskFileName, Count);
    if (VIP_NULL == items) {
        return -1;
    }

    *taskes = items;

    return ret;
}

vip_status_e query_hardware_info(void)
{
    vip_uint32_t version = vip_get_version();
    vip_uint32_t device_count = 0;
    vip_uint32_t cid = 0;
    vip_uint32_t *core_count = VIP_NULL;
    vip_uint32_t i = 0;

    if (version >= 0x00010601) {
        vip_query_hardware(VIP_QUERY_HW_PROP_CID, sizeof(vip_uint32_t), &cid);
        vip_query_hardware(VIP_QUERY_HW_PROP_DEVICE_COUNT, sizeof(vip_uint32_t), &device_count);
        core_count = (vip_uint32_t*)malloc(sizeof(vip_uint32_t) * device_count);
        vip_query_hardware(VIP_QUERY_HW_PROP_CORE_COUNT_EACH_DEVICE,
                          sizeof(vip_uint32_t) * device_count, core_count);
        printf("cid=0x%x, device_count=%d\n", cid, device_count);
        for (i = 0; i < device_count; i++) {
            printf("  device[%d] core_count=%d\n", i, core_count[i]);
        }
        free(core_count);
    }
    return VIP_SUCCESS;
}

/* Create the network in the task. */
static vip_status_e vpm_create_network(
    vpm_network_task_t *task,
    vip_uint32_t network_id
    )
{
    vip_status_e status = VIP_SUCCESS;
    char *file_name = VIP_NULL;
    void *output_map = VIP_NULL;
    vip_uint64_t file_size = 0;
    int i = 0;
    int input_count = 0;
    int output_count = 0;
    vip_buffer_create_params_t param;

    /* Load nbg data. */
    file_name = task->base_strings[task->nbg_name];
    file_size = get_file_size((const char *) file_name);
    if (file_size <= 0) {
        printf("Network binary file %s can't be found.\n", file_name);
        status = VIP_ERROR_INVALID_ARGUMENTS;
        return status;
    }

#ifdef CREATE_NETWORK_FROM_MEMORY
    network_buffer[network_id] = malloc(file_size);
    load_file(file_name, network_buffer[network_id]);

    #if defined (__linux__)
    TimeBegin(1);
    #endif

    status = vip_create_network(network_buffer[network_id], file_size, VIP_CREATE_NETWORK_FROM_MEMORY,
                                &task->network);
    free(network_buffer[network_id]);
    network_buffer[network_id] = VIP_NULL;

#elif CREATE_NETWORK_FROM_FILE
    #if defined (__linux__)
    TimeBegin(1);
    #endif

    status = vip_create_network(file_name, 0, VIP_CREATE_NETWORK_FROM_FILE, &task->network);

#elif CREATE_NETWORK_FROM_FLASH
    /* This is a demo code for DDR-less project.
       You don't need to allocate this memory if you are in DDR-less products.
       You can use vip_create_network() function to create a network.
       network_buffer is the staring address of flash */
    network_buffer[network_id] = vsi_nn_MallocAlignedBuffer(file_size, 4096, 4096);
    load_file(file_name, network_buffer[network_id]);

#if defined (__linux__)
    TimeBegin(1);
#endif

    status = vip_create_network(network_buffer[network_id], file_size, VIP_CREATE_NETWORK_FROM_FLASH,
                                &task->network);
#endif
    if (status != VIP_SUCCESS) {
        printf("Network creating failed. Please validate the content of %s.\n", file_name);
        return status;
    }

#if defined (__linux__)
    TimeEnd(1);
    printf("nbg name=%s\n", file_name);
    printf("create network %d: %lu us.\n", network_id, (unsigned long)TimeGet(1));
#endif

    /* Create input buffers. */
    vip_query_network(task->network, VIP_NETWORK_PROP_INPUT_COUNT, &input_count);
    if (input_count != task->input_count) {
        printf("Error: input count mismatch. Required inputs by network: %d, actually provided: %d.\n",
                input_count, task->input_count);
        status = VIP_ERROR_MISSING_INPUT_OUTPUT;
        return status;
    }

    task->input_buffers = (vip_buffer *)malloc(sizeof(vip_buffer) * task->input_count);
    for (i = 0; i < task->input_count; i++) {
        vip_char_t name[256];
        memset(&param, 0, sizeof(param));
        param.memory_type = VIP_BUFFER_MEMORY_TYPE_DEFAULT;
        vip_query_input(task->network, i, VIP_BUFFER_PROP_DATA_FORMAT, &param.data_format);
        vip_query_input(task->network, i, VIP_BUFFER_PROP_NUM_OF_DIMENSION, &param.num_of_dims);
        vip_query_input(task->network, i, VIP_BUFFER_PROP_SIZES_OF_DIMENSION, param.sizes);
        vip_query_input(task->network, i, VIP_BUFFER_PROP_QUANT_FORMAT, &param.quant_format);
        vip_query_input(task->network, i, VIP_BUFFER_PROP_NAME, name);
        switch(param.quant_format) {
            case VIP_BUFFER_QUANTIZE_DYNAMIC_FIXED_POINT:
                vip_query_input(task->network, i, VIP_BUFFER_PROP_FIXED_POINT_POS,
                                &param.quant_data.dfp.fixed_point_pos);
                break;
            case VIP_BUFFER_QUANTIZE_TF_ASYMM:
                vip_query_input(task->network, i, VIP_BUFFER_PROP_TF_SCALE,
                                &param.quant_data.affine.scale);
                vip_query_input(task->network, i, VIP_BUFFER_PROP_TF_ZERO_POINT,
                                &param.quant_data.affine.zeroPoint);
            default:
            break;
        }

        printf("input %d dim %d %d %d %d, data_format=%d, quant_format=%d, name=%s",
               i, param.sizes[0], param.sizes[1], param.sizes[2], param.sizes[3],
               param.data_format, param.quant_format, name);

        switch(param.quant_format) {
            case VIP_BUFFER_QUANTIZE_DYNAMIC_FIXED_POINT:
                printf(", dfp=%d\n", param.quant_data.dfp.fixed_point_pos);
                break;
            case VIP_BUFFER_QUANTIZE_TF_ASYMM:
                printf(", scale=%f, zero_point=%d\n", param.quant_data.affine.scale,
                       param.quant_data.affine.zeroPoint);
                break;
            default:
                printf(", none-quant\n");
        }

        status = vip_create_buffer(&param, sizeof(param), &task->input_buffers[i]);
        if (status != VIP_SUCCESS) {
            printf("fail to create input %d buffer, status=%d\n", i, status);
            return status;
        }
    }

    /* Create output buffer. */
    vip_query_network(task->network, VIP_NETWORK_PROP_OUTPUT_COUNT, &output_count);
    if (task->output_count != 0 && output_count != task->output_count) {
        printf("Error: output count mismatch. Required output_counts by network: %d, actually provided: %d.\n",
                output_count, task->output_count);
        status = VIP_ERROR_MISSING_INPUT_OUTPUT;
        return status;
    }

    task->output_count = output_count;
    task->output_buffers = (vip_buffer *)malloc(sizeof(vip_buffer) * task->output_count);
    for (i = 0; i < task->output_count; i++) {
        vip_char_t name[256];
        memset(&param, 0, sizeof(param));
        param.memory_type = VIP_BUFFER_MEMORY_TYPE_DEFAULT;
        vip_query_output(task->network, i, VIP_BUFFER_PROP_DATA_FORMAT, &param.data_format);
        vip_query_output(task->network, i, VIP_BUFFER_PROP_NUM_OF_DIMENSION, &param.num_of_dims);
        vip_query_output(task->network, i, VIP_BUFFER_PROP_SIZES_OF_DIMENSION, param.sizes);
        vip_query_output(task->network, i, VIP_BUFFER_PROP_QUANT_FORMAT, &param.quant_format);
        vip_query_output(task->network, i, VIP_BUFFER_PROP_NAME, name);
        switch(param.quant_format) {
            case VIP_BUFFER_QUANTIZE_DYNAMIC_FIXED_POINT:
                vip_query_output(task->network, i, VIP_BUFFER_PROP_FIXED_POINT_POS,
                                 &param.quant_data.dfp.fixed_point_pos);
                break;
            case VIP_BUFFER_QUANTIZE_TF_ASYMM:
                vip_query_output(task->network, i, VIP_BUFFER_PROP_TF_SCALE,
                                 &param.quant_data.affine.scale);
                vip_query_output(task->network, i, VIP_BUFFER_PROP_TF_ZERO_POINT,
                                 &param.quant_data.affine.zeroPoint);
                break;
            default:
            break;
        }

        printf("ouput %d dim %d %d %d %d, data_format=%d, name=%s",
               i, param.sizes[0], param.sizes[1], param.sizes[2], param.sizes[3],
               param.data_format, name);

        switch(param.quant_format) {
            case VIP_BUFFER_QUANTIZE_DYNAMIC_FIXED_POINT:
                printf(", dfp=%d\n", param.quant_data.dfp.fixed_point_pos);
                break;
            case VIP_BUFFER_QUANTIZE_TF_ASYMM:
                printf(", scale=%f, zero_point=%d\n", param.quant_data.affine.scale,
                       param.quant_data.affine.zeroPoint);
                break;
            default:
                printf(", none-quant\n");
        }

        status = vip_create_buffer(&param, sizeof(param), &task->output_buffers[i]);
         if (status != VIP_SUCCESS) {
             printf("fail to create output %d buffer, status=%d\n", i, status);
            return status;
         }
        /* memset output_buffer to zero */
        {
            vip_uint32_t k = 0;
            vip_uint32_t buf_size = type_get_bits(param.data_format);
            for (k = 0; k < param.num_of_dims; k++) {
                buf_size *= param.sizes[k];
                if (0 == k) {
                    /* round up */
                    buf_size = (buf_size + BITS_PER_BYTE - 1) / BITS_PER_BYTE;
                }
            }
            output_map = vip_map_buffer(task->output_buffers[i]);
            vip_memset(output_map, buf_size);
            vip_flush_buffer(task->output_buffers[i], VIP_BUFFER_OPER_TYPE_INVALIDATE);
            vip_unmap_buffer(task->output_buffers[i]);
            output_map = VIP_NULL;
        }
    }

    {
        vip_uint32_t mem_pool_size = 0;
        vip_uint8_t core_count = 0;
        vip_query_network(task->network, VIP_NETWORK_PROP_MEMORY_POOL_SIZE, &mem_pool_size);
        printf("memory pool size=%dbyte\n", mem_pool_size);
        vip_query_network(task->network, VIP_NETWORK_PROP_CORE_COUNT, &core_count);
        printf("network core count=%d\n", core_count);
        task->core_count = core_count;
    }

    return status;
}

/* set network before vip_prepare_network() */
vip_status_e vpm_set_network(
    vpm_network_task_t *task
    )
{
    vip_status_e status = VIP_SUCCESS;

    /* the default device index is 0. we need chang it when not use device 0.*/
    if (task->param.device_index > 0) {
        printf("vpm run start set device index=%d.\n", task->param.device_index);
#if NPU_SW_VERSION >= 2
        status = vip_set_network(task->network, VIP_NETWORK_PROP_SET_DEVICE_INDEX, &task->param.device_index);
#else
        status = vip_set_network(task->network, VIP_NETWORK_PROP_SET_DEVICE_ID, &task->param.device_index);
#endif
        if (status != VIP_SUCCESS) {
            printf("vpm run set device index fail, index = %d.\n", task->param.device_index);
            goto onError;
        }
        printf("vpm run set device index success, index = %d.\n", task->param.device_index);
    }

#if NPU_SW_VERSION >= 2
    /* the defalt core index is -1. */
    if (-1 != task->param.core_index) {
        vip_uint32_t k = 0;
        printf("vpm run set dev index=%d, core index=%d, core cnt=%d, network run id:",
               task->param.device_index, task->param.core_index, task->core_count);
        for (k = 0; k < task->core_count; k++) {
            printf(" %d", task->param.core_index + k);
        }
        printf("\n");
        status = vip_set_network(task->network, VIP_NETWORK_PROP_SET_CORE_INDEX, &task->param.core_index);
        if (status != VIP_SUCCESS) {
            printf("vpm run set core index fail, index = %d.\n", task->param.core_index);
            goto onError;
        }
    }

    /* enable npd by set network */
    if (task->param.enable_npd == vip_true_e) {
        status = vip_set_network(task->network, VIP_NETWORK_PROP_SET_ENABLE_NPD, &task->param.enable_npd);
        if (status != VIP_SUCCESS) {
            printf("fail to set enable npd.\n");
            goto onError;
        }
    }

    /* enable preload coeff into vipsram by set network */
    if (task->param.preload_vipsram == vip_true_e) {
        status = vip_set_network(task->network, VIP_NETWORK_PROP_SET_VIPSRAM_PRELOAD, &task->param.preload_vipsram);
        if (status != VIP_SUCCESS) {
            printf("fail to set preload coeff into vipsram.\n");
            goto onError;
        }
    }
#endif
    if (0 != task->param.time_out) {
        status = vip_set_network(task->network, VIP_NETWORK_PROP_SET_TIME_OUT, &task->param.time_out);
        if (status != VIP_SUCCESS) {
            printf("fail to set time out of network\n");
            goto onError;
        }
    }

    if ((task->param.start_op != -1) && (task->param.end_op != -1)) {
        vip_uint32_t op_id[2] = {task->param.start_op, task->param.end_op};
        status = vip_set_network(task->network, 1024, op_id);
        if (status != VIP_SUCCESS) {
            printf("fail to set operation segment\n");
            goto onError;
        }
    }

onError:
    return status;
}

vip_status_e load_golden_data(vpm_network_task_t *task)
{
    vip_status_e status = VIP_SUCCESS;
    vip_int32_t i = 0 ;
    char *golden_name = VIP_NULL;
    file_type_e file_type = 0;

    if (task->golden_count > 0) {
        if (VIP_NULL == task->golden_data ) {
            task->golden_data = malloc(sizeof(void*) * task->golden_count);
            memset(task->golden_data, 0, (sizeof(void*) * task->golden_count));
        }
        if (VIP_NULL == task->golden_size) {
            task->golden_size = malloc(sizeof(vip_uint64_t) * task->golden_count);
        }
    }

    printf("golden file count=%d\n", task->golden_count);
    for (i = 0; i < task->golden_count; i++) {
        if (task->base_strings[task->golden_names[i]] != VIP_NULL) {
            golden_name = task->base_strings[task->golden_names[i]];
            printf("golden %d read file=%s\n", i, golden_name);
            file_type = get_file_type(golden_name);
            if (file_type != NN_FILE_BINARY) {
                printf("can't read golden file only support binary format\n");
                continue;
            }
            task->golden_size[i] = get_file_size(golden_name);
            if (0 == task->golden_size[i]) {
                printf("    fail to read golden_%d name=%s\n", i, golden_name);
                continue;
            }
            else {
                printf("golden %d read file size=%lld\n", i, task->golden_size[i]);
            }

            task->golden_data[i] = malloc(task->golden_size[i]);
            load_file(golden_name, (void *)task->golden_data[i]);
        }
    }

    return status;
}

vip_status_e free_golden_data(vpm_network_task_t *task)
{
    int i = 0;

    if (task->golden_data != VIP_NULL) {
        for (i = 0; i < task->golden_count; i++) {
            if (task->golden_data[i] != VIP_NULL) {
                free(task->golden_data[i]);
                task->golden_data[i] = VIP_NULL;
            }
        }

        free(task->golden_data);
        task->golden_data = VIP_NULL;
    }
    if (task->golden_size != VIP_NULL) {
        free(task->golden_size);
        task->golden_size = VIP_NULL;
    }

    return VIP_SUCCESS;
}

vip_status_e load_input_data(vpm_network_task_t *task)
{
    vip_status_e status = VIP_SUCCESS;
    void *data;
    void *file_data = VIP_NULL;
    char *file_name;
    vip_uint64_t file_size;
    vip_uint32_t buff_size;
    int i;

    /* Load input buffer data. */
    for (i = 0; i < task->input_count; i++) {
        file_type_e file_type;
        file_name = task->base_strings[task->input_names[i]];
        printf("input %d name: %s\n", i , file_name);
        file_type = get_file_type(file_name);

        switch(file_type)
        {
            case NN_FILE_TENSOR:
                file_data = (void *)get_tensor_data(task, file_name, &file_size, i);
                break;
            case NN_FILE_BINARY:
                file_data = (void *)get_binary_data(file_name, &file_size);
                break;
            case NN_FILE_TEXT:
                file_data = (void *)get_tensor_data(task, file_name, &file_size, i);
                break;
            default:
                printf("error input file type\n");
                break;
        }

        if (0 == file_size) {
            printf("fail to read input %d file=%s\n", i, file_name);
            return VIP_ERROR_FAILURE;
        }

        data = vip_map_buffer(task->input_buffers[i]);
        buff_size = vip_get_buffer_size(task->input_buffers[i]);
        vip_memcpy(data, file_data, buff_size > file_size ? file_size : buff_size);
        vip_unmap_buffer(task->input_buffers[i]);

        if (file_data != VIP_NULL) {
            free(file_data);
            file_data = VIP_NULL;
        }
    }

    return status;
}

/* Create buffers, and configure the netowrk in the task. */
vip_status_e set_network_input_output(vpm_network_task_t *task)
{
    vip_status_e status = VIP_SUCCESS;
    int i = 0;

    /* Load input buffer data. */
    for (i = 0; i < task->input_count; i++) {
        /* Set input. */
        status = vip_set_input(task->network, i, task->input_buffers[i]);
        if (status != VIP_SUCCESS) {
            printf("fail to set input %d\n", i);
            goto ExitFunc;
        }
    }

    for (i = 0; i < task->output_count; i++) {
        if (task->output_buffers[i] != VIP_NULL) {
            status = vip_set_output(task->network, i, task->output_buffers[i]);
            if (status != VIP_SUCCESS) {
                printf("fail to set output\n");
                goto ExitFunc;
            }
        }
        else {
            printf("fail output %d is null. output_counts=%d\n", i, task->output_count);
            status = VIP_ERROR_FAILURE;
            goto ExitFunc;
        }
    }

ExitFunc:
    return status;
}

static vip_int32_t get_operation_id(const char *op_id_str, vip_uint32_t *start_op, vip_uint32_t *end_op)
{
    vip_uint32_t tmp_s = 0, tmp_e = 0, i = 0;
    vip_int32_t ret = 0;
    vip_uint32_t len = strlen(op_id_str);
    char *attrp = VIP_NULL;

    tmp_s = atoi(op_id_str);

    for (i = 0; i < len; i++) {
        if (',' == op_id_str[i]) {
            attrp = (char *)(&op_id_str[i] + 1);
            tmp_e = atoi(attrp);
        }
    }

    *start_op = tmp_s;
    *end_op = tmp_e;
    printf("set operation id [%d ~ %d]\n", tmp_s, tmp_e);

    if (tmp_e < tmp_s) {
        printf("not support set operation id [%d ~ %d]\n", tmp_s, tmp_e);
        ret = -1;
    }

    return ret;
}

static vip_bool_e get_top(
    float *pf_prob,
    float *pf_max_prob,
    unsigned int *max_class,
    unsigned int out_put_count,
    unsigned int top_num
    )
{
    unsigned int i, j;

    if (top_num > 10) return vip_false_e;

    memset(pf_max_prob, 0xfe, sizeof(float) * top_num);
    memset(max_class, 0xff, sizeof(float) * top_num);
    for (j = 0; j < top_num; j++) {
        for (i=0; i<out_put_count; i++) {
            if ((i == *(max_class+0)) || (i == *(max_class+1)) || (i == *(max_class+2)) ||
                (i == *(max_class+3)) || (i == *(max_class+4)))
                continue;
            if (pf_prob[i] > *(pf_max_prob+j)) {
                *(pf_max_prob+j) = pf_prob[i];
                *(max_class+j) = i;
            }
        }
    }

    return vip_true_e;
}

void show_result(
    void* buffer,
    vip_uint32_t buffer_size,
    unsigned int element_cnt,
    signed int data_type,
    vip_int32_t quant_format,
    unsigned char fix_pos,
    vip_int32_t zeroPoint,
    vip_float_t scale
    )
{
    vip_status_e status = VIP_SUCCESS;
    unsigned int i = 0;
    unsigned int max_class[5];
    float fMaxProb[5];
    float *outBuf = VIP_NULL;
    vip_uint8_t *src_buffer = (vip_uint8_t *)buffer;
    vip_uint32_t stride = type_get_bytes(data_type);

    outBuf = (float *)malloc(element_cnt * sizeof(float));
    memset(outBuf, 0, element_cnt * sizeof(float));
    if(outBuf == NULL) {
        printf("Can't malloc space\n");
    }

    /* sanity check size */
    if (buffer_size < element_cnt * stride) {
        printf("show resut element_cnt=%u, buffer_size=%u, stride=%u\n", element_cnt, buffer_size, stride);
        if(outBuf != NULL) {
            free(outBuf);
        }
        return;
    }

    for (i = 0; i < element_cnt; i++) {
        status = dtype_to_float32(&src_buffer[stride * i], &outBuf[i], data_type,
                                 quant_format, fix_pos, scale, zeroPoint);
        if (status != VIP_SUCCESS) {
            printf("not support dtype to fp32\n");
            free(outBuf);
            return;
        }
    }

    if (!get_top((float*)outBuf, fMaxProb, max_class, element_cnt, 5)) {
        printf("Fail to show result.\n");
    }

    printf(" --- Top5 ---\n");

    for (i=0; i < 5; i++) {
        printf("%3d: %8.6f\n", max_class[i], (float)fMaxProb[i]);
    }

    free(outBuf);
}

int save_txt_file(
    void* buffer,
    unsigned int element_cnt,
    signed int data_type,
    vip_int32_t quant_format,
    unsigned char fix_pos,
    vip_int32_t zeroPoint,
    vip_float_t scale,
    char *filename
    )
{
    #define TMPBUF_SZ  (512)
    vip_uint32_t status = VIP_SUCCESS;
    vip_uint32_t i = 0;
    int ret = 0;
    FILE        *fp;
    float fp_data = 0.0;
    vip_uint8_t buf[TMPBUF_SZ];
    vip_uint32_t count = 0;
    vip_uint8_t *src_buffer = (vip_uint8_t *)buffer;
    vip_uint32_t stride = type_get_bytes(data_type);

    fp = fopen(filename, "w");

    for (i = 0; i < element_cnt; i++) {
        status = dtype_to_float32(&src_buffer[stride * i], &fp_data, data_type,
                                  quant_format, fix_pos, scale, zeroPoint);
        if (status != VIP_SUCCESS) {
            printf("not support dtype to fp32\n");
            ret = -1;
            goto error;
        }
        count += sprintf((char *)&buf[count], "%.16f%s", fp_data, "\n");
        if ((count + 50) > TMPBUF_SZ) {
            fwrite(buf, count, 1, fp );
            count = 0;
        }
    }

    fwrite(buf, count, 1, fp );
    fflush(fp);
error:
    fclose(fp );

    return ret;
}

vip_int32_t inference_profile(
    vpm_network_task_t *task,
    vip_uint32_t count
    )
{
    vip_inference_profile_t profile;
    vip_int32_t ret = 0;
    vip_uint32_t tolerance = 1000; /* 1000us */
    vip_uint32_t time_diff = 0;

    vip_query_network(task->network, VIP_NETWORK_PROP_PROFILING, &profile);
    printf("profile inference time=%uus, cycle=%u\n", profile.inference_time,
           profile.total_cycle);
    if (1 == count) {
        task->infer_cycle = profile.total_cycle;
        task->infer_time = profile.inference_time;
    }
    else {
        vip_float_t rate = (vip_float_t)task->infer_cycle / (vip_float_t)profile.total_cycle;
        time_diff = (task->infer_time > profile.inference_time) ? (task->infer_time - profile.inference_time) : \
                     (profile.inference_time - task->infer_time);
        if (((rate > 1.05) || (rate < 0.95)) && (time_diff > tolerance)) {
            ret = -1;
        }
    }

    task->total_infer_cycle += (vip_uint64_t)task->infer_cycle;
    task->total_infer_time += (vip_uint64_t)task->infer_time;

    return ret;
}

vip_int32_t vpm_check_result(vpm_network_task_t *task, vip_uint32_t show_top5, vip_uint32_t save_txt_output)
{
    char *out_name = VIP_NULL;
    void *out_data = VIP_NULL;
    void* out_data_4bit = VIP_NULL;
    vip_int32_t j = 0;
    vip_int32_t ret = 0;
    vip_uint32_t k = 0;
    vip_int32_t data_format = 0;
    vip_int32_t output_fp  = 0;
    vip_int32_t quant_format = 0;
    vip_int32_t output_counts = 0;
    vip_uint32_t output_size = 0;
    vip_int32_t zeroPoint = 0;
    vip_float_t scale;
    vip_buffer_create_params_t param;
    vip_bool_e is_odd_bit4s = vip_false_e;

    vip_query_network(task->network, VIP_NETWORK_PROP_OUTPUT_COUNT, &output_counts);

    for (j = 0; j < output_counts; j++) {
        unsigned int output_element = 1;
        char output_txt_filename[255] = {'\0'};
        sprintf(output_txt_filename, "output_%d.txt", j);
        memset(&param, 0, sizeof(param));
        vip_query_output(task->network, j, VIP_BUFFER_PROP_QUANT_FORMAT, &quant_format);
        vip_query_output(task->network, j, VIP_BUFFER_PROP_TF_SCALE,
                         &param.quant_data.affine.scale);
        scale = param.quant_data.affine.scale;
        vip_query_output(task->network, j, VIP_BUFFER_PROP_TF_ZERO_POINT,
                           &param.quant_data.affine.zeroPoint);
        zeroPoint = param.quant_data.affine.zeroPoint;
        vip_query_output(task->network, j, VIP_BUFFER_PROP_DATA_FORMAT,
                         &param.data_format);
        data_format = param.data_format;
        vip_query_output(task->network, j, VIP_BUFFER_PROP_NUM_OF_DIMENSION,
                         &param.num_of_dims);
        vip_query_output(task->network, j, VIP_BUFFER_PROP_FIXED_POINT_POS,
                         &param.quant_data.dfp.fixed_point_pos);
        output_fp = param.quant_data.dfp.fixed_point_pos;
        vip_query_output(task->network, j, VIP_BUFFER_PROP_SIZES_OF_DIMENSION, param.sizes);

        output_size = type_get_bits(data_format);
        for (k = 0; k < param.num_of_dims; k++) {
            output_element *= param.sizes[k];
            output_size *= param.sizes[k];
            if (0 == k) {
                /* round up */
                output_size = (output_size + BITS_PER_BYTE - 1) / BITS_PER_BYTE;
            }
        }

        out_data = vip_map_buffer(task->output_buffers[j]);

        if (VIP_BUFFER_FORMAT_INT4 == data_format || VIP_BUFFER_FORMAT_UINT4 == data_format) {
            is_odd_bit4s = (output_size % 2 == 0) ? vip_false_e : vip_true_e;
            out_data_4bit = malloc(output_element * sizeof(vip_uint8_t));
            memset(out_data_4bit, 0, output_element * sizeof(vip_uint8_t));
            unpack_4bit_data(out_data, out_data_4bit, output_size, param.sizes[0],
                VIP_BUFFER_FORMAT_INT4 == data_format ? vip_true_e : vip_false_e);
            pack_4bit_data(out_data_4bit, out_data, output_element, param.sizes[0]);
        #ifdef SAVE_OUTPUT_TXT_FILE
            if (save_txt_output) {
                /* save output to .txt file */
                save_txt_file(out_data_4bit, output_element, VIP_BUFFER_FORMAT_UINT8,
                    quant_format, output_fp, zeroPoint, scale, output_txt_filename);
            }
        #endif

            /* save [output] file */
            if ((task->output_names != NULL) && (task->base_strings[task->output_names[j]] != VIP_NULL)) {
                file_type_e file_type = 0;
                out_name = task->base_strings[task->output_names[j]];
                file_type = get_file_type(out_name);
                printf("output %d file type=%d\n", j, file_type);
                if (NN_FILE_BINARY == file_type) {
                    save_file(out_name, out_data, output_size);
                }
                else if ((NN_FILE_TENSOR == file_type) || (NN_FILE_TEXT == file_type)) {
                    save_txt_file(out_data_4bit, output_element, VIP_BUFFER_FORMAT_UINT8,
                                  quant_format, output_fp, zeroPoint, scale, out_name);
                }
            }
        }
        else {
        #ifdef SAVE_OUTPUT_TXT_FILE
            if (save_txt_output) {
                /* save output to .txt file */
                save_txt_file(out_data, output_element, data_format, quant_format,
                            output_fp, zeroPoint, scale, output_txt_filename);
            }
        #endif

            /* save [output] file */
            if ((task->output_names != NULL) && (task->base_strings[task->output_names[j]] != VIP_NULL)) {
                file_type_e file_type = 0;
                out_name = task->base_strings[task->output_names[j]];
                file_type = get_file_type(out_name);
                printf("output %d file type=%d\n", j, file_type);
                if (NN_FILE_BINARY == file_type) {
                    save_file(out_name, out_data, output_size);
                }
                else if ((NN_FILE_TENSOR == file_type) || (NN_FILE_TEXT == file_type)) {
                    save_txt_file(out_data, output_element, data_format, quant_format,
                                    output_fp, zeroPoint, scale, out_name);
                }
            }
        }

        /* show top5 */
        #ifdef SHOW_TOP5
        if (show_top5) {
            if ((j < task->golden_count) && (task->golden_data != VIP_NULL) && (task->golden_data[j] != VIP_NULL)) {
                if (output_size <= task->golden_size[j]) {
                    printf("******* golden TOP5 ********\n");
                    if (VIP_BUFFER_FORMAT_INT4 == data_format || VIP_BUFFER_FORMAT_UINT4 == data_format) {
                        void* buffer_tmp = malloc(output_element * sizeof(vip_uint8_t));
                        memset(buffer_tmp, 0, output_element * sizeof(vip_uint8_t));
                        unpack_4bit_data(task->golden_data[j], buffer_tmp, output_size, param.sizes[0],
                            VIP_BUFFER_FORMAT_INT4 == data_format ? vip_true_e : vip_false_e);
                        show_result(buffer_tmp, output_element * sizeof(vip_uint8_t), output_element, data_format,
                                    quant_format, output_fp, zeroPoint, scale);
                        free(buffer_tmp);
                    }
                    else {
                        show_result(task->golden_data[j], task->golden_size[j], output_element, data_format,
                                    quant_format, output_fp, zeroPoint, scale);
                    }
                }
            }
            printf("******* nb TOP5 ********\n");
            if (VIP_BUFFER_FORMAT_INT4 == data_format || VIP_BUFFER_FORMAT_UINT4 == data_format) {
                vip_uint32_t out_size_u8 = output_element * sizeof(vip_uint8_t);
                show_result(out_data_4bit, out_size_u8, output_element,
                            data_format, quant_format, output_fp, zeroPoint, scale);
            }
            else {
                show_result(out_data, output_size, output_element,
                            data_format, quant_format, output_fp, zeroPoint, scale);
            }
        }
        #endif

        /* compare with [golden] file */
        if ((j < task->golden_count) && (task->golden_data != VIP_NULL) && (task->golden_data[j] != VIP_NULL)) {
            /* Check result. */
            vip_bool_e is_equal = vip_false_e;
            /* check int4 or uint4 output num is odd */
            if (is_odd_bit4s) {
                is_equal = (memcmp(out_data, task->golden_data[j], task->golden_size[j] - 1) == 0) ?
                    vip_true_e : vip_false_e;
                is_equal &= compare_low_4bits((vip_uint8_t*)out_data + output_size -1,
                        (vip_uint8_t*)task->golden_data[j] + output_size -1);
            }
            else {
                is_equal = (memcmp(out_data, task->golden_data[j], task->golden_size[j]) == 0) ?
                    vip_true_e : vip_false_e;
            }

            if (!is_equal) {
                char fail_name[255] = {'\0'};
                sprintf(fail_name, "failed_output_%d.dat", j);
                save_file(fail_name, out_data, output_size);
                printf("    Test output %d failed: data mismatch. Output saved in file %s "
                       "for further analysis.\n", j, fail_name);
                if (VIP_BUFFER_FORMAT_INT4 != data_format && VIP_BUFFER_FORMAT_UINT4 != data_format) {
                    sprintf(fail_name, "failed_output_%d.txt", j);
                    save_txt_file(out_data, output_element, data_format, quant_format,
                                    output_fp, zeroPoint, scale, fail_name);
                }
                ret = -1;
                vip_memset(out_data, output_size);
            }
            else {
                vip_memset(out_data, task->golden_size[j]);
                printf("    Test output %d passed.\n\n", j);
                if ((vip_flush_buffer(task->output_buffers[j], VIP_BUFFER_OPER_TYPE_FLUSH)) != VIP_SUCCESS) {
                    printf("flush output%d cache failed.\n", j);
                }
            }
        }

        vip_unmap_buffer(task->output_buffers[j]);
        if (VIP_BUFFER_FORMAT_INT4 == data_format || VIP_BUFFER_FORMAT_UINT4 == data_format) {
            free(out_data_4bit);
        }
    }

    return ret;
}

vip_status_e vxnneAccess(
    const char *path,
    vip_int32_t mode
    )
{
    if(NULL == path) {
        return -1;
    }

#ifdef __linux__
    return access(path, mode);
#else
    return _access(path, mode);
#endif
} /* vxnneAccess() */

vip_status_e vxnneMkdir(
    const char *path,
    vip_int32_t mode
    )
{
    if(VIP_NULL == path) {
        return -1;
    }

#ifdef __linux__
    return mkdir(path, mode);
#else
    return _mkdir(path);
#endif
} /* vxnneMkdir() */

vip_bool_e vxnneCheckFilePath(
    const char *path
    )
{
    if (VIP_NULL == path) {
        printf("Please set file path\n");
        return vip_false_e;
    }

    if (vxnneAccess(path, 0) == 0) {
        return vip_true_e;
    }

    if (vxnneMkdir(path, 0775) == 0) {
        printf("Create directory %s\n", path);
        return vip_true_e;
    }
    else {
        printf("Create directory %s fail\n", path);
    }

    return vip_false_e;
} /* vxnneCheckFilePath() */

#if NPU_SW_VERSION >= 2
vip_status_e get_nld_layer_id(
    const char *attr,
    vip_nld_layer_id_t *nld_layer_id
    )
{
    vip_status_e status = VIP_SUCCESS;
    char *attrp = VIP_NULL;
    vip_int32_t count = 0;
    vip_int32_t *list_id = VIP_NULL;

    vip_uint32_t len = strlen(attr);
    vip_uint32_t i = 0;
    vip_int32_t id = 0;
    vip_uint32_t index = 0;

    if (!nld_layer_id) {
        return VIP_ERROR_FAILURE;
    }

    if (!attr) {
        return VIP_ERROR_FAILURE;
    }

    for (i = 0; i < len; i++) {
        if (',' == attr[i]) {
            count++;
        }
    }
    count++; /* the last layer id */

    list_id = (vip_int32_t *)malloc(count * sizeof(vip_int32_t));
    vip_memset((vip_uint8_t *)list_id, count * sizeof(vip_int32_t));

    attrp = (char *)attr;
    id = atoi(attrp);
    list_id[index++] = id;
    for (i = 0; i < len; i++) {
        if (',' == attr[i]) {
            attrp = (char *)(&attr[i] + 1);
            id = atoi(attrp);
            list_id[index++] = id;
        }
    }

    if (list_id[0] == -1) {
        printf("dump all layer output\n");
        count = -1;
    }

    nld_layer_id->layer_count = count;
    nld_layer_id->layer_id = list_id;

    return status;
}

vip_status_e save_nld_output_file(
    vpm_network_task_t *task,
    vip_nld_output_t *nld_output
    )
{
    vip_status_e status = VIP_SUCCESS;

    vip_uint32_t nld_output_index = 0;
    vip_uint32_t nld_output_count = 0;
    char fileFolderName[128] = { '\0' };

    sprintf(fileFolderName, "CommandDumpViplite");
    if (!vxnneCheckFilePath(fileFolderName)) {
        printf("can't creat the floder %s\n", fileFolderName);
        goto exit;
    }

    nld_output_count = nld_output->count;
    for (nld_output_index = 0; nld_output_index < nld_output_count; nld_output_index++) {
        vip_nld_output_info_t *nld_output_info = nld_output->info + nld_output_index;
        char filename[255] = {'\0'};
        vip_uint32_t output_element = 1;
        vip_uint32_t i = 0;

        if (nld_output_info->uid == ~0) {
            sprintf(filename, "%s/layerOut_%d_NodeID_%d_%s.txt", fileFolderName, nld_output_info->layer_output_index,
                    nld_output_info->layer_id, nld_output_info->layer_name);
        }
        else {
            sprintf(filename, "%s/layerOut_%d_uid_%d_NodeID_%d_%s.txt", fileFolderName,
                nld_output_info->layer_output_index, nld_output_info->uid,
                nld_output_info->layer_id, nld_output_info->layer_name);
        }
        printf("nld_%d filenem=%s\n", nld_output_index, filename);
        for (i = 0; i < nld_output_info->param.dim_count; i++) {
            output_element *= nld_output_info->param.dim_size[i];
        }
        if (VIP_BUFFER_QUANTIZE_DYNAMIC_FIXED_POINT == nld_output_info->param.quant_format) {
            printf("nld_%d element count=%u, data_fmt=%d, quan_fmt=%d, dfp=%d\n",
               nld_output_index, output_element, nld_output_info->param.data_format,
               nld_output_info->param.quant_format, nld_output_info->param.quant_data.dfp.fixed_point_pos);
        }
        else {
            printf("nld_%d element count=%u, data_fmt=%d, quan_fmt=%d, scale=%f, zp=%d\n",
               nld_output_index, output_element, nld_output_info->param.data_format,
               nld_output_info->param.quant_format, nld_output_info->param.quant_data.affine.tf_scale,
               nld_output_info->param.quant_data.affine.tf_zero_point);
        }
        save_txt_file(nld_output_info->memory,
                    output_element,
                    nld_output_info->param.data_format,
                    nld_output_info->param.quant_format,
                    nld_output_info->param.quant_data.dfp.fixed_point_pos,
                    nld_output_info->param.quant_data.affine.tf_zero_point,
                    nld_output_info->param.quant_data.affine.tf_scale,
                    filename);
    }

exit:
    return status;
}

vip_status_e set_nbg_layer_dump(
    vpm_network_task_t *task,
    vip_nld_layer_id_t *nld_layer_id,
    vip_nld_output_t *nld_output
    )
{
    vip_status_e status = VIP_SUCCESS;
    vip_network network = VIP_NULL;

    network = task->network;

    status = vip_set_network(network, VIP_NETWORK_PROP_SET_LAYER_DUMP_ID, nld_layer_id);
    if (status != VIP_SUCCESS) {
        printf("fail to set nbg layer dump.\n");
        goto exit;
    }

    status = vip_query_network(network, VIP_NETWORK_PROP_GET_LAYER_DUMP_OUTPUT, nld_output);
    if (status != VIP_SUCCESS) {
        printf("fail to get output information of dumped layer.\n");
        goto exit;
    }

exit:
    return status;
}
#endif

//VmSize:the current virtual memory size of the process
//VmHWM: the peak physical memory size of the process
//VmRSS:the current physical memory size of the process
static const char *GREP_KEY = "-E \"VmSize|VmRSS|VmHWM\"";
void print_mem(const char *prefix, const char *grep)
{
    int pid = getpid();
    char command[256];
    sprintf(command, "cat /proc/%d/status | grep %s", pid, grep);
    printf("==== %s \n", prefix);
    system(command);
    printf("\n");
}

int main(int argc, char* argv[])
{
    vip_status_e status = VIP_SUCCESS;
    vip_char_t *file_name = VIP_NULL;
    vip_int32_t task_count = 0;
    vip_int32_t i = 0, k = 0;
    vip_uint32_t loop_count = 1;
    vip_uint32_t count = 0;
    vip_int32_t ret = 0;
    vip_uint32_t version = 0;
    vip_uint32_t device_index = 0;
    vip_uint32_t core_index = -1;
    vpm_network_task_t *tasks = VIP_NULL;
    vip_uint32_t time_out = 0;
    vip_uint32_t hardware_bypass = 1;
    vip_uint32_t profile_memory_usage = 0;
    vip_uint32_t show_top5 = 0;
    vip_uint32_t save_txt_output = 0;
    vip_uint32_t start_op = -1, end_op = -1;
    vip_bool_e enable_npd = vip_false_e;
    vip_bool_e preload_vipsram = vip_false_e;
    vpm_network_task_t *task = VIP_NULL;

#if NPU_SW_VERSION >= 2
    vip_nld_layer_id_t nld_layer_id = {0};
    vip_nld_output_t nld_output = {0};
#endif

    if (argc < 2) {
        printf("%s\n", usage);
        return -1;
    }

    for (i = 0; i< argc; i++) {
        if (!strcmp(argv[i], "-l")) {
            loop_count = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-d")) {
            device_index = atoi(argv[++i]);
        }
#if NPU_SW_VERSION >= 2
        else if (!strcmp(argv[i], "-c")) {
            core_index = atoi(argv[++i]);
        }
#endif
        else if (!strcmp(argv[i], "-s")) {
            file_name = argv[++i];
        }
        else if (!strcmp(argv[i], "-t")) {
            time_out = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-b")) {
            hardware_bypass = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-m")) {
            profile_memory_usage = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--show_top5")) {
            show_top5 = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--save_txt")) {
            save_txt_output = atoi(argv[++i]);
        }
#if NPU_SW_VERSION >= 2
        else if (!strcmp(argv[i], "--layer_profile_dump")) {
            enable_npd = (vip_bool_e)atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--preload")) {
            preload_vipsram = (vip_bool_e)atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--op_segment")) {
            get_operation_id(argv[++i], &start_op, &end_op);
        }
        else if (!strcmp(argv[i], "--layer_dump")) {
            get_nld_layer_id(argv[++i], &nld_layer_id);
        }
#endif
        else if (!strcmp(argv[i], "-h")) {
            printf("%s\n", usage);
            return 0;
        }
    }

    if (profile_memory_usage == 1) {
        setbuf(stdout, NULL);
    }

#if NPU_SW_VERSION >= 2
    printf("loop_count=%d, device_index=%d, core_index=%d, file_name=%s, time_out=0x%x, bypass=%d\n",
        loop_count, device_index, core_index, file_name, time_out, hardware_bypass);
    printf("enable_npd=%d, preload=%d\n", enable_npd, preload_vipsram);
    printf("show_top5%d, save_txt=%d\n", show_top5, save_txt_output);
#else
    printf("loop_count=%d, device_id=%d, file_name=%s bypass=%d\n",
        loop_count, device_index, file_name, hardware_bypass);
#endif

    if (VIP_NULL == file_name) {
        printf("%s\n", usage);
        return -1;
    }

    if (profile_memory_usage == 1) {
        print_mem("before vip_init", GREP_KEY);
    }

    version = vip_get_version();
    printf("init vip lite, driver version=0x%08x...\n", version);

    status = vip_init();
    if (status != VIP_SUCCESS) {
        printf("failed to init vip\n");
        ret = -1;
        goto exit;
    }
    printf("vip lite init OK.\n\n");

    if (profile_memory_usage == 1) {
        print_mem("afrer vip_init", GREP_KEY);
    }

    query_hardware_info();

    ret = init_test_resources(&tasks, file_name, &task_count);
    if (ret < 0) {
        printf("fail to get resource file_name=%s\n", file_name);
        return ret;
    }
    printf("init test resources, task_count: %d ...\n", task_count);

    for (i = 0; i < task_count; i++) {
        tasks[i].loop_count = loop_count;
    }

    printf("create/prepare networks ...\n");
    if (tasks != VIP_NULL) {
        for (i = 0; i < task_count; i++) {
            task = &tasks[i];
            printf("task i=%d, binary name: %s\n", i, task->base_strings[task->nbg_name]);
            status = vpm_create_network(task, i);
            if (status != VIP_SUCCESS) {
                printf("create network %d failed.\n", i);
                ret = -1;
                goto exit;
            }

            task->param.enable_npd = enable_npd;
            task->param.preload_vipsram = preload_vipsram;
            task->param.time_out = time_out;
            task->param.start_op = start_op;
            task->param.end_op = end_op;
            task->param.device_index = device_index;
            task->param.core_index = core_index;
            status = vpm_set_network(task);
            if (status != VIP_SUCCESS) {
                printf("fail to set network status=%d\n", status);
                ret = -1;
                goto exit;
            }

#if NPU_SW_VERSION >= 2
            if (nld_layer_id.layer_count) {
                status = set_nbg_layer_dump(task, &nld_layer_id, &nld_output);
                if (status != VIP_SUCCESS) {
                    printf("fail to set nbg layer dump.\n");
                    ret = -1;
                    goto exit;
                }
            }
#endif

            #if defined (__linux__)
            TimeBegin(2);
            #endif
            /* Prepare network. */
            status = vip_prepare_network(task->network);
            if (status != VIP_SUCCESS) {
                printf("fail prpare network, status=%d\n", status);
                ret = -1;
                goto exit;
            }

            #if defined (__linux__)
            TimeEnd(2);
            printf("prepare network %d: %lu us.\n", i, (unsigned long) TimeGet(2));
            #endif

            #if defined (__linux__)
            TimeBegin(2);
            #endif
            load_golden_data(task);
            status = load_input_data(task);
            if (status != VIP_SUCCESS) {
                ret = -1;
                goto exit;
            }
            #if defined (__linux__)
            TimeEnd(2);
            printf("read input and golden %d: %lu us.\n", i, (unsigned long) TimeGet(2));
            #endif
        }

        /* run network */
        while(count < loop_count) {
            count++;
            for (i = 0; i < task_count; i++) {
                task = &tasks[i];
                printf("task: %d, loop count: %d\n", i, count);
                status = set_network_input_output(task);
                if (status != VIP_SUCCESS) {
                    printf("set network input/output %d failed.\n", i);
                    ret = -1;
                    goto exit;
                }

                printf("start to run network=%s\n", task->base_strings[task->nbg_name]);
                #if defined (__linux__)
                TimeBegin(0);
                #endif
                /* it is only necessary to call vip_flush_buffer() after set vpmdENABLE_FLUSH_CPU_CACHE to 2 */
                for (k = 0; k < task->input_count; k++) {
                    if ((vip_flush_buffer(task->input_buffers[k], VIP_BUFFER_OPER_TYPE_FLUSH)) != VIP_SUCCESS) {
                        printf("flush input%d cache failed.\n", k);
                    }
                }

                if (profile_memory_usage == 1) {
                    if (count == 1) {
                        print_mem("before vip_run_network", GREP_KEY);
                    }
                }

                status = vip_run_network(task->network);
                if (status != VIP_SUCCESS) {
                    if (status == VIP_ERROR_CANCELED) {
                        printf("network is canceled.\n");
                        ret = VIP_ERROR_CANCELED;
                        goto exit;
                    }
                    printf("fail to run network, status=%d, taskCount=%d\n", status, i);
                    ret = -2;
                    goto exit;
                }

                if (profile_memory_usage == 1) {
                    if (count == 1) {
                        print_mem("after vip_run_network", GREP_KEY);
                    }
                }

                for (k = 0; k < task->output_count; k++) {
                  if ((vip_flush_buffer(task->output_buffers[k], VIP_BUFFER_OPER_TYPE_INVALIDATE)) != VIP_SUCCESS){
                      printf("flush output%d cache failed.\n", k);
                    }
                }

                #if defined (__linux__)
                TimeEnd(0);
                printf("run time for this network %d: %lu us.\n", i, (unsigned long) TimeGet(0));
                #endif
                printf("run network done...\n");

#if NPU_SW_VERSION >= 2
                if (nld_layer_id.layer_count) {
                    save_nld_output_file(task, &nld_output);
                    free(nld_layer_id.layer_id);
                    nld_layer_id.layer_id = VIP_NULL;
                    nld_layer_id.layer_count = 0;
                }
#endif
                inference_profile(task, count);

                if (hardware_bypass == 0) {
                    ret = vpm_check_result(task, show_top5, save_txt_output);
                    if (ret != 0) {
                        goto exit;
                    }
                }
            }
        };

        if (loop_count > 1) {
            for (i = 0; i < task_count; i++) {
                task = &tasks[i];
                printf("task %d, profile avg inference time=%dus, cycle=%d\n", i,
                    (vip_uint32_t)(task->total_infer_time / task->loop_count),
                    (vip_uint32_t)(task->total_infer_cycle / task->loop_count));
            }
        }
    }
    else {
        printf("failed to read %s\n", file_name);
    }

exit:
    if (tasks != VIP_NULL) {
        for (i = 0; i < task_count; i++) {
            free_golden_data(&tasks[i]);

            vip_finish_network(tasks[i].network);

            destroy_network(&tasks[i]);

            if (network_buffer[i] != VIP_NULL) {
                #ifdef CREATE_NETWORK_FROM_FLASH
                vsi_nn_FreeAlignedBuffer((vip_uint8_t*)network_buffer[i]);
                #else
                free(network_buffer[i]);
                #endif
                network_buffer[i] = VIP_NULL;
            }
        }
    }

    for (i = 0; i < task_count; i++) {
        if (network_buffer[i] != VIP_NULL) {
            free(network_buffer[i]);
            network_buffer[i] = VIP_NULL;
        }
    }

    destroy_test_resources(tasks, task_count);

    status = vip_destroy();
    if (status != VIP_SUCCESS) {
        printf("fail to destory vip\n");
    }

    printf("vpm run ret=%d\n", ret);
    return ret;
}
