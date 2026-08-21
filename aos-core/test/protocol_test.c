/* 線上格式的純函式檢查：編碼、解碼、以及各種壞輸入。不開任何 socket。 */
#include "check.h"

#include <string.h>

#include "buf.h"
#include "protocol.h"

static void test_header_round_trip(void) {
    unsigned char header[AOS_FRAME_HEADER_SIZE];
    aos_encode_header(header, 0x01020304u, AOS_FRAME_STDOUT_CHUNK);

    /* 大端寫死，不看本機位元序。 */
    AOS_CHECK(header[0] == 0x01 && header[1] == 0x02);
    AOS_CHECK(header[2] == 0x03 && header[3] == 0x04);

    uint32_t len = 0;
    unsigned kind = 0;
    aos_decode_header(header, &len, &kind);
    AOS_CHECK(len == 0x01020304u);
    AOS_CHECK(kind == AOS_FRAME_STDOUT_CHUNK);
}

static void test_zero_length_is_legal(void) {
    /* 長度不含 kind 那個位元組，所以 0 是合法的——stdin_end 就是這樣。
     * 把 kind 算進長度的話 0 會變成不合法，那是很容易踩到的邊界。 */
    unsigned char header[AOS_FRAME_HEADER_SIZE];
    aos_encode_header(header, 0, AOS_FRAME_STDIN_END);

    uint32_t len = 1;
    unsigned kind = 0;
    aos_decode_header(header, &len, &kind);
    AOS_CHECK(len == 0);
    AOS_CHECK(kind == AOS_FRAME_STDIN_END);
}

static void test_known_kinds(void) {
    AOS_CHECK(aos_frame_kind_known(AOS_FRAME_REQUEST_START));
    AOS_CHECK(aos_frame_kind_known(AOS_FRAME_EXIT));
    AOS_CHECK(!aos_frame_kind_known(0));
    AOS_CHECK(!aos_frame_kind_known(99));
}

static void test_request_round_trip(void) {
    /* 空字串、引號、空白、非 ASCII 都要原樣回來。 */
    const char *argv[] = {"new", "bot alpha", "", "--verbose", "中文\"引號\""};
    buf payload = BUF_INIT;
    AOS_CHECK(aos_encode_request(&payload, 5, argv, "/tmp/some dir") == 0);

    aos_request decoded;
    char reason[256] = {0};
    AOS_CHECK(aos_decode_request(payload.data, payload.len, &decoded, reason,
                                 sizeof reason) == 0);
    AOS_CHECK(decoded.argc == 5);
    for (int i = 0; i < 5; ++i) {
        AOS_CHECK(strcmp(decoded.argv[i], argv[i]) == 0);
    }
    AOS_CHECK(strcmp(decoded.working_directory, "/tmp/some dir") == 0);

    aos_request_free(&decoded);
    buf_free(&payload);
}

static void test_empty_argv_round_trip(void) {
    buf payload = BUF_INIT;
    AOS_CHECK(aos_encode_request(&payload, 0, NULL, "/") == 0);

    aos_request decoded;
    char reason[256] = {0};
    AOS_CHECK(aos_decode_request(payload.data, payload.len, &decoded, reason,
                                 sizeof reason) == 0);
    AOS_CHECK(decoded.argc == 0);
    AOS_CHECK(strcmp(decoded.working_directory, "/") == 0);

    aos_request_free(&decoded);
    buf_free(&payload);
}

static void test_bad_requests_are_rejected(void) {
    aos_request decoded;
    char reason[256];

    /* 空的。 */
    AOS_CHECK(aos_decode_request("", 0, &decoded, reason, sizeof reason) != 0);

    /* 版本不對：要拒絕，不要猜。 */
    unsigned char wrong_version[] = {0x00, 0x63, 0, 0, 0, 0, 0, 0, 0, 0};
    AOS_CHECK(aos_decode_request(wrong_version, sizeof wrong_version, &decoded,
                                 reason, sizeof reason) != 0);
    AOS_CHECK(strstr(reason, "協定版本") != NULL);

    /* 說有 3 個參數但後面什麼都沒有：不能讀出界。 */
    unsigned char lying_argc[] = {0x00, 0x02, 0x00, 0x00, 0x00, 0x03};
    AOS_CHECK(aos_decode_request(lying_argc, sizeof lying_argc, &decoded, reason,
                                 sizeof reason) != 0);

    /* argc 大得不合理：不能照著它去配置。 */
    unsigned char huge_argc[] = {0x00, 0x02, 0xFF, 0xFF, 0xFF, 0xFF};
    AOS_CHECK(aos_decode_request(huge_argc, sizeof huge_argc, &decoded, reason,
                                 sizeof reason) != 0);
}

static void test_trailing_bytes_are_rejected(void) {
    const char *argv[] = {"ping"};
    buf payload = BUF_INIT;
    aos_encode_request(&payload, 1, argv, "/");
    buf_adds(&payload, "多出來的");

    aos_request decoded;
    char reason[256];
    /* 多出來的位元組代表兩邊對格式的理解不一樣，寧可拒絕也不要默默忽略。 */
    AOS_CHECK(aos_decode_request(payload.data, payload.len, &decoded, reason,
                                 sizeof reason) != 0);
    buf_free(&payload);
}

static void test_exit_round_trip(void) {
    unsigned char payload[4];
    int32_t code = 0;

    aos_encode_exit(payload, 0);
    AOS_CHECK(aos_decode_exit(payload, 4, &code) == 0 && code == 0);

    aos_encode_exit(payload, 2);
    AOS_CHECK(aos_decode_exit(payload, 4, &code) == 0 && code == 2);

    /* 負數也要能來回，exit code 在協定上是有號的。 */
    aos_encode_exit(payload, -1);
    AOS_CHECK(aos_decode_exit(payload, 4, &code) == 0 && code == -1);

    /* 長度不對就是壞的。 */
    AOS_CHECK(aos_decode_exit(payload, 3, &code) != 0);
}

static void test_buf_handles_binary_and_growth(void) {
    buf b = BUF_INIT;
    /* 含 \0 的資料要完整保留——這一層從頭到尾不靠 NUL 結尾。 */
    AOS_CHECK(buf_add(&b, "a\0b", 3) == 0);
    AOS_CHECK(b.len == 3);
    AOS_CHECK(b.data[1] == '\0' && b.data[2] == 'b');

    /* 逼它成長好幾次。 */
    for (int i = 0; i < 10000; ++i) {
        buf_addf(&b, "%d,", i);
    }
    AOS_CHECK(!b.failed);
    AOS_CHECK(b.len > 10000);

    buf_drop_front(&b, 3);
    AOS_CHECK(b.data[0] == '0' && b.data[1] == ',');

    AOS_CHECK(buf_cstr(&b) == 0);
    AOS_CHECK(b.data[b.len] == '\0');
    buf_free(&b);
}

int main(void) {
    test_header_round_trip();
    test_zero_length_is_legal();
    test_known_kinds();
    test_request_round_trip();
    test_empty_argv_round_trip();
    test_bad_requests_are_rejected();
    test_trailing_bytes_are_rejected();
    test_exit_round_trip();
    test_buf_handles_binary_and_growth();
    return aos_report();
}
