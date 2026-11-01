/* GRAY8 IR file + a face box in that IR image. Threshold is device-calibrated.
 * Usage: hiface_sdk_infrared_liveness frame.gray W H STRIDE X Y FACE_W FACE_H THRESHOLD
 */
#include <hiface/hiface_linux.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

static int parse_dimension(const char* text, int* result) {
    char* end = NULL;
    long value = strtol(text, &end, 10);
    if (!text[0] || !end || *end || value < 1 || value > INT_MAX) return 0;
    *result = (int)value;
    return 1;
}

int main(int argc, char** argv) {
    if (argc != 10) {
        fprintf(stderr, "Usage: %s frame.gray WIDTH HEIGHT STRIDE X Y FACE_W FACE_H THRESHOLD\n", argv[0]);
        return 2;
    }
    HiFaceInfraredLivenessRequestV1 r = {0};
    r.struct_size = sizeof(r);
    if (!parse_dimension(argv[2], &r.width) || !parse_dimension(argv[3], &r.height)
            || !parse_dimension(argv[4], &r.row_stride) || r.width > 4096 || r.height > 4096
            || r.row_stride < r.width) return 2;
    float* values[] = {&r.face_x, &r.face_y, &r.face_width, &r.face_height, &r.real_threshold};
    for (int i = 0; i < 5; ++i) {
        char* end = NULL; *values[i] = strtof(argv[5+i], &end);
        if (!argv[5+i][0] || !end || *end) return 2;
    }
    FILE* image = fopen(argv[1], "rb");
    if (!image || fseek(image, 0, SEEK_END) != 0) { if (image) fclose(image); return 2; }
    long bytes = ftell(image);
    if (bytes < 1 || bytes > 64L*1024*1024 || fseek(image, 0, SEEK_SET) != 0) { fclose(image); return 2; }
    uint8_t* gray = (uint8_t*)malloc((size_t)bytes);
    if (!gray || fread(gray, 1, (size_t)bytes, image) != (size_t)bytes) {
        free(gray); fclose(image); return 2;
    }
    fclose(image); r.gray = gray; r.gray_size = (size_t)bytes;
    HiFaceRuntimeConfigV1 runtime = {0}; runtime.struct_size = sizeof(runtime);
    runtime.api_version = HIFACE_C_API_VERSION; runtime.cpu_threads = 2;
    HiFaceStatus status = hiface_runtime_configure_v1(&runtime);
    HiFaceConfigV1 config = {0}; config.struct_size = sizeof(config); config.api_version = HIFACE_C_API_VERSION;
    HiFaceSessionHandle session = NULL;
    if (status == HIFACE_STATUS_OK) status = hiface_session_create_v1(&config, &session);
    if (status == HIFACE_STATUS_OK) status = hiface_linux_load_infrared_model_v1(session, NULL);
    HiFaceInfraredLivenessResultV1 result = {0}; result.struct_size = sizeof(result);
    if (status == HIFACE_STATUS_OK) status = hiface_session_evaluate_infrared_liveness_v1(session, &r, &result);
    if (status == HIFACE_STATUS_OK)
        printf("real_score=%.9g spoof_score=%.9g passed=%u\n", result.real_score, result.spoof_score, result.passed);
    else fprintf(stderr, "IR inference failed: status=%d\n", status);
    hiface_session_destroy_v1(session); free(gray);
    return status == HIFACE_STATUS_OK ? 0 : 1;
}
