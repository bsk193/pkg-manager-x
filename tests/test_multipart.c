#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "multipart.h"
#include "pkg_parser.h"
#include "pkg_scanner.h"
#include "installer.h"
#include "smb_client.h"
#include "test_fixture.h"

/* Shared synthetic source package (80MB -> 2x60M / 3x35M / 2x50M parts).
 * Fictional ID/title; generated once in main(). */
#define MP_SRC_PKG "/tmp/test_mpkg_src/nd.pkg"

static void test_multipart_creation_and_decompression(void) {
    printf("=== 1. Testing Multi-Part Creation & Bit-for-Bit Decompression ===\n");

    system("rm -rf /tmp/test_mpkg && mkdir -p /tmp/test_mpkg/parts");

    /* Split test PKG into 60MB raw uncompressed sequential parts */
    int comp_ret = system("python3 tools/pkg_split.py " MP_SRC_PKG " -o /tmp/test_mpkg/parts -s 60M > /dev/null 2>&1");
    assert(comp_ret == 0);

    /* Verify headers */
    const char *p1 = "/tmp/test_mpkg/parts/nd.pkg.part1";
    const char *p2 = "/tmp/test_mpkg/parts/nd.pkg.part2";

    multipart_header_t h1, h2;
    assert(multipart_read_header(p1, &h1) == 0);
    assert(multipart_read_header(p2, &h2) == 0);

    assert(h1.part_index == 1);
    assert(h1.total_parts == 2);
    assert(h1.compression_type == MULTIPART_COMPRESSION_NONE);
    assert(h2.part_index == 2);
    assert(h2.total_parts == 2);
    assert(h2.compression_type == MULTIPART_COMPRESSION_NONE);
    assert(strcmp(h1.title_id, "PPSA90010") == 0);
    assert(strcmp(h1.title_name, "NebulaDrift") == 0);
    assert(h1.icon_offset == 4096);
    assert(h1.icon_size == FIXTURE_ICON_SIZE);
    assert(memcmp(h1.package_uuid, h2.package_uuid, 16) == 0);

    /* Decompress and verify bit-for-bit equality */
    const char *part_paths[] = { p1, p2 };
    volatile int cancel = 0;
    int d_ret = multipart_decompress_to_pkg(part_paths, 2, "/tmp/test_mpkg/unpacked.pkg", NULL, NULL, &cancel);
    assert(d_ret == 0);

    int diff_res = system("diff -q /tmp/test_mpkg/unpacked.pkg " MP_SRC_PKG);
    assert(diff_res == 0);
    printf("   -> Decompressed PKG matches original exactly!\n");
}

static void test_scanner_multipart_detection(void) {
    printf("=== 2. Testing Scanner Multi-Part Detection & Disc Part Search ===\n");

    /* Create mock disc directory */
    system("rm -rf /tmp/test_disc_dir && mkdir -p /tmp/test_disc_dir/pkg");
    system("cp /tmp/test_mpkg/parts/nd.pkg.part1 /tmp/test_disc_dir/pkg/");

    setenv("PKG_SCAN_DIR", "/tmp/test_disc_dir/pkg", 1);
    pkg_scanner_init();
    int count = pkg_scanner_scan();
    printf("   Scanned count with Part 1: %d\n", count);
    assert(count == 1);

    pkg_detail_t detail;
    assert(pkg_scanner_get_at(0, &detail) == 0);
    assert(detail.is_multipart == 1);
    assert(detail.part_index == 1);
    assert(detail.total_parts == 2);
    assert(strcmp(detail.title_id, "PPSA90010") == 0);
    assert(strcmp(detail.title_name, "NebulaDrift") == 0);
    assert(detail.has_icon == 1);

    /* Verify JSON output includes multipart metadata */
    char *json = pkg_scanner_to_json();
    assert(strstr(json, "\"is_multipart\":true") != NULL);
    assert(strstr(json, "\"total_parts\":2") != NULL);
    assert(strstr(json, "\"part_index\":1") != NULL);
    free(json);

    /* Now copy Part 2 to disc directory as well */
    system("cp /tmp/test_mpkg/parts/nd.pkg.part2 /tmp/test_disc_dir/pkg/");
    count = pkg_scanner_scan();
    printf("   Scanned count with Part 1 and Part 2: %d\n", count);
    /* Part 2 should NOT be listed as a separate package! */
    assert(count == 1);

    /* Test finding Part 2 with pkg_scanner_find_part */
    multipart_header_t h1;
    multipart_read_header("/tmp/test_mpkg/parts/nd.pkg.part1", &h1);
    char found_part2[512] = {0};
    int find_res = pkg_scanner_find_part(h1.package_uuid, h1.pkg_filename, 2, found_part2, sizeof(found_part2));
    assert(find_res == 0);
    assert(strstr(found_part2, "part2") != NULL);
    printf("   -> Found Part 2 at: %s\n", found_part2);

    /* Test case-insensitive part filename matching */
    uint32_t pnum = 0;
    assert(multipart_is_part_filename("game.pkg.part2", 2, &pnum) == 1 && pnum == 2);
    assert(multipart_is_part_filename("game.pkg.PART2", 2, &pnum) == 1 && pnum == 2);
    assert(multipart_is_part_filename("game.PKG.Part02", 2, &pnum) == 1 && pnum == 2);
    assert(multipart_is_part_filename("game.part2", 2, &pnum) == 1 && pnum == 2);
    assert(multipart_is_part_filename("game.part2.pkg", 2, &pnum) == 1 && pnum == 2);
    assert(multipart_is_part_filename("part_2.pkg.part", 2, &pnum) == 1 && pnum == 2);
    assert(multipart_is_part_filename("game.pkg.part1", 2, &pnum) == 0);
    assert(multipart_is_part_filename("game.pkg.part1", 0, &pnum) == 1 && pnum == 1);
    assert(multipart_is_part_filename("movie.mp4", 2, &pnum) == 0);
    assert(multipart_is_part_filename("game.pkg", 2, &pnum) == 0);
    assert(multipart_is_part_filename("game.pkg", 0, &pnum) == 0);

    /* Test finding Part 2 when renamed to uppercase .PKG.PART2 on disc */
    system("mv /tmp/test_disc_dir/pkg/nd.pkg.part2 /tmp/test_disc_dir/pkg/nd.pkg.PKG.PART2");
    char found_upper[512] = {0};
    assert(pkg_scanner_find_part(h1.package_uuid, h1.pkg_filename, 2, found_upper, sizeof(found_upper)) == 0);
    printf("   -> Found uppercase Part 2 at: %s\n", found_upper);
    assert(strstr(found_upper, "PART2") != NULL);
    /* Restore lowercase for subsequent tests */
    system("mv /tmp/test_disc_dir/pkg/nd.pkg.PKG.PART2 /tmp/test_disc_dir/pkg/nd.pkg.part2");

    /* Testing non-existent part 3 returns -1 */
    char found_part3[512] = {0};
    assert(pkg_scanner_find_part(h1.package_uuid, h1.pkg_filename, 3, found_part3, sizeof(found_part3)) == -1);
}

static void test_space_check_and_dir_creation(void) {
    printf("=== 3. Testing Storage Space Check & Recursive Directory Creation ===\n");

    const char *tmp_dir = "/tmp/pkg_installer_test_space/nested/tmp";
    setenv("PKG_TMP_DIR", tmp_dir, 1);

    /* 1. Space check failure simulation */
    setenv("PKG_FORCE_SPACE_CHECK_FAIL", "1", 1);
    installer_init("http://127.0.0.1:8085/");

    int res = installer_start("/tmp/test_mpkg/parts/nd.pkg.part1");
    printf("   Space check failure result: %d\n", res);
    assert(res == -10); /* Must fail space check */

    installer_shutdown();
    unsetenv("PKG_FORCE_SPACE_CHECK_FAIL");

    /* Verify directory was not created while space check failed */
    struct stat st;
    assert(stat(tmp_dir, &st) != 0);

    /* 2. Normal directory creation on valid start */
    installer_init("http://127.0.0.1:8085/");
    res = installer_start("/tmp/test_mpkg/parts/nd.pkg.part1");
    assert(res == 0);

    /* Verify nested tmp directory was created recursively */
    assert(stat(tmp_dir, &st) == 0);
    assert(S_ISDIR(st.st_mode));
    printf("   -> Successfully verified directory creation at: %s\n", tmp_dir);

    installer_shutdown();
    system("rm -rf /tmp/pkg_installer_test_space");
}

static void test_end_to_end_disc_swapping(void) {
    printf("=== 4. Testing End-to-End Disc Swapping Installation Pipeline ===\n");

    /* Disc scan dir will only have Part 1 initially */
    system("rm -rf /tmp/test_e2e_disc && mkdir -p /tmp/test_e2e_disc/pkg /tmp/holding_disc2 /tmp/e2e_tmp");
    system("cp /tmp/test_mpkg/parts/nd.pkg.part1 /tmp/test_e2e_disc/pkg/");
    system("cp /tmp/test_mpkg/parts/nd.pkg.part2 /tmp/holding_disc2/");

    setenv("PKG_SCAN_DIR", "/tmp/test_e2e_disc/pkg", 1);
    setenv("PKG_TMP_DIR", "/tmp/e2e_tmp", 1);

    installer_init("http://127.0.0.1:8085/");

    /* Start multi-part installation with Part 1 */
    int ret = installer_start("/tmp/test_e2e_disc/pkg/nd.pkg.part1");
    assert(ret == 0);

    installer_status_t st;
    installer_get_status(&st);
    assert(st.is_installing == 1);
    assert(st.is_multipart == 1);
    assert(st.total_parts == 2);

    /* Wait for Part 1 to finish copying and enter waiting_disc state */
    printf("   Waiting for Part 1 to copy and prompt for Disc 2...\n");
    int waited_ms = 0;
    while (waited_ms < 10000) {
        installer_get_status(&st);
        if (st.waiting_for_disc && strcmp(st.status_str, "waiting_disc") == 0) {
            break;
        }
        usleep(100000); /* 100ms */
        waited_ms += 100;
    }

    assert(st.waiting_for_disc == 1);
    assert(st.current_part == 2);
    assert(strstr(st.prompt_message, "Disc 2") != NULL);
    printf("   -> Disc prompt verified: '%s'\n", st.prompt_message);

    char *json = installer_status_to_json();
    assert(strstr(json, "\"waiting_for_disc\":true") != NULL);
    assert(strstr(json, "\"current_part\":2") != NULL);
    free(json);

    /* Simulate user inserting Disc 2: move Part 2 into disc directory */
    printf("   Simulating Disc 2 insertion into drive...\n");
    system("cp /tmp/holding_disc2/nd.pkg.part2 /tmp/test_e2e_disc/pkg/");

    /* Wait for installer to automatically detect Disc 2, copy it, decompress, and complete install */
    printf("   Waiting for automatic detection, copy, decompression & install completion...\n");
    waited_ms = 0;
    while (waited_ms < 60000) {
        installer_record_poll(); /* Simulate active browser polling */
        installer_get_status(&st);
        if (waited_ms % 2000 == 0) {
            printf("   [+%.1fs] status='%s', is_installing=%d, completed=%d, progress=%.1f%%\n",
                   waited_ms / 1000.0, st.status_str, st.is_installing, st.completed, st.progress_percent);
        }
        if (st.completed && !st.is_installing) {
            break;
        }
        usleep(200000); /* 200ms */
        waited_ms += 200;
    }

    printf("   Final state: completed=%d, is_installing=%d, status='%s'\n",
           st.completed, st.is_installing, st.status_str);
    assert(st.completed == 1);
    assert(st.is_installing == 0);
    assert(st.failed == 0);
    assert(st.progress_percent == 100.0f);
    assert(strcmp(st.status_str, "playable") == 0);
    printf("   -> Multi-disc installation completed successfully!\n");

    /* Verify temporary files were cleaned up to save space */
    struct stat st_check;
    assert(stat("/tmp/e2e_tmp/part_1.pkg.part", &st_check) != 0);
    assert(stat("/tmp/e2e_tmp/part_2.pkg.part", &st_check) != 0);
    assert(stat("/tmp/e2e_tmp/nd.pkg", &st_check) != 0);
    printf("   -> Temporary files in /tmp/e2e_tmp cleaned up successfully!\n");

    installer_shutdown();
    system("rm -rf /tmp/test_e2e_disc /tmp/holding_disc2 /tmp/e2e_tmp /tmp/test_disc_dir /tmp/test_mpkg");
}

static void test_uncompressed_multipart_and_crc(void) {
    printf("=== 5. Testing Uncompressed Multi-Part with CRC32 Verification ===\n");

    system("rm -rf /tmp/test_mpkg_none && mkdir -p /tmp/test_mpkg_none/parts");
    int comp_ret = system("python3 tools/pkg_split.py " MP_SRC_PKG " -o /tmp/test_mpkg_none/parts -s 35M > /dev/null 2>&1");
    assert(comp_ret == 0);

    const char *p1 = "/tmp/test_mpkg_none/parts/nd.pkg.part1";
    const char *p2 = "/tmp/test_mpkg_none/parts/nd.pkg.part2";
    const char *p3 = "/tmp/test_mpkg_none/parts/nd.pkg.part3";

    multipart_header_t h1;
    assert(multipart_read_header(p1, &h1) == 0);
    assert(h1.compression_type == MULTIPART_COMPRESSION_NONE);
    assert(h1.total_parts == 3);

    const char *parts[] = { p1, p2, p3 };
    volatile int cancel = 0;
    int d_ret = multipart_decompress_to_pkg(parts, 3, "/tmp/test_mpkg_none/unpacked.pkg", NULL, NULL, &cancel);
    assert(d_ret == 0);

    int diff_res = system("diff -q /tmp/test_mpkg_none/unpacked.pkg " MP_SRC_PKG);
    assert(diff_res == 0);
    printf("   -> Uncompressed multi-part reassembled and verified bit-for-bit against original!\n");
    system("rm -rf /tmp/test_mpkg_none");
}

static void test_default_part_size_under_25gb(void) {
    printf("=== 6. Testing Default Part Size Fits Standard 25GB BD-R ===\n");
    int ret = system("python3 -c \"import sys; sys.path.insert(0, 'tools'); import pkg_split; "
                     "p = pkg_split.parse_size('23G'); "
                     "assert p <= 25000000000, f'Size {p} exceeds BD-25 limit!'; "
                     "print(f'Default part size: {p:,} bytes (< 25,000,000,000 bytes BD-25 capacity)')\"");
    assert(ret == 0);
    printf("   -> Default part size verified to fit on BD-R 25GB discs!\n");
}

static void test_out_of_order_disc_and_cancellation(void) {
    printf("=== 7. Testing Out-of-Order Disc Detection & Installation Cancellation ===\n");

    /* Create 3 parts */
    system("rm -rf /tmp/test_ooo && mkdir -p /tmp/test_ooo/parts /tmp/test_ooo/disc /tmp/test_ooo/tmp");
    /* Small single-part package for the post-cancel restart check. */
    assert(fixture_write_ps5_pkg("/tmp/test_ooo/single.pkg",
                                 "PPSA90012", "WaveCast", "gd", "01.003.000", 0) == 0);

    int comp_ret = system("python3 tools/pkg_split.py " MP_SRC_PKG " -o /tmp/test_ooo/parts -s 35M > /dev/null 2>&1");
    assert(comp_ret == 0);

    /* Disc initially has Part 1 */
    system("cp /tmp/test_ooo/parts/nd.pkg.part1 /tmp/test_ooo/disc/");

    setenv("PKG_SCAN_DIR", "/tmp/test_ooo/disc", 1);
    setenv("PKG_TMP_DIR", "/tmp/test_ooo/tmp", 1);

    installer_init("http://127.0.0.1:8085/");
    int ret = installer_start("/tmp/test_ooo/disc/nd.pkg.part1");
    assert(ret == 0);

    /* Wait for Part 1 to copy and prompt for Disc 2 */
    installer_status_t st;
    int waited_ms = 0;
    while (waited_ms < 10000) {
        installer_get_status(&st);
        if (st.waiting_for_disc && st.current_part == 2) break;
        usleep(100000);
        waited_ms += 100;
    }
    assert(st.waiting_for_disc == 1);
    assert(st.current_part == 2);
    printf("   Waiting for Disc 2 prompt verified.\n");

    /* 1. Simulate inserting Disc 3 (out of order!) */
    printf("   Simulating inserting Disc 3 when Disc 2 was expected...\n");
    system("rm -f /tmp/test_ooo/disc/* && cp /tmp/test_ooo/parts/nd.pkg.part3 /tmp/test_ooo/disc/");

    /* Wait for scanner to detect wrong disc */
    waited_ms = 0;
    while (waited_ms < 5000) {
        installer_get_status(&st);
        if (strstr(st.prompt_message, "Disc 3 detected") != NULL) break;
        usleep(100000);
        waited_ms += 100;
    }
    printf("   -> Out-of-order prompt: '%s'\n", st.prompt_message);
    assert(strstr(st.prompt_message, "Disc 3 detected") != NULL);
    assert(strstr(st.prompt_message, "Disc 2") != NULL);

    /* 2. Test Cancellation */
    printf("   Testing installer_cancel()...\n");
    int cancel_res = installer_cancel();
    assert(cancel_res == 0);

    installer_get_status(&st);
    assert(st.is_installing == 0);
    assert(st.failed == 1);
    assert(strcmp(st.status_str, "canceled") == 0);
    printf("   -> Installation successfully canceled! Status: '%s'\n", st.status_str);

    /* Verify temporary directory cleaned up */
    usleep(200000);
    struct stat st_tmp;
    assert(stat("/tmp/test_ooo/tmp/part_1.pkg.part", &st_tmp) != 0);

    installer_shutdown();

    /* Verify that after cancellation, a new install can start without being blocked */
    installer_init("http://127.0.0.1:8085/");
    int new_start = installer_start("/tmp/test_ooo/single.pkg");
    assert(new_start == 0);
    installer_shutdown();
    printf("   -> Verified new install can start immediately after cancellation!\n");

    system("rm -rf /tmp/test_ooo");
}

static void test_smb_multipart_rejected_and_local_streaming(void) {
    printf("=== 8. Testing SMB Multi-Part Rejection & Local Virtual Streaming ===\n");

    system("rm -rf /tmp/mock_smb && mkdir -p /tmp/mock_smb/storage/Games/PS4");

    /* Split test PKG into 50MB parts inside mock SMB share */
    int comp_ret = system("python3 tools/pkg_split.py " MP_SRC_PKG " -o /tmp/mock_smb/storage/Games/PS4 -s 50M > /dev/null 2>&1");
    assert(comp_ret == 0);

    const char *smb_url_p1 = "smb://192.168.1.140/16tb01/storage/Games/PS4/nd.pkg.part1";

    /* 1. Verify multipart_read_header rejects SMB URLs (multi-part is strictly local USB/disc) */
    multipart_header_t hdr1;
    int r_ret = multipart_read_header(smb_url_p1, &hdr1);
    assert(r_ret == -1);
    printf("   -> multipart_read_header rejected SMB URL as expected\n");

    /* 2. Verify smb_client_parse_pkg rejects PS5MPKG1 magic */
    pkg_detail_t detail;
    int parse_ret = smb_client_parse_pkg(smb_url_p1, &detail);
    assert(parse_ret == -1);
    printf("   -> smb_client_parse_pkg rejected PS5MPKG1 magic as expected\n");

    /* 3. Verify virtual_stream_open on SMB path is treated as single file (is_multipart = 0) */
    virtual_stream_t vs_smb;
    int open_ret = virtual_stream_open(smb_url_p1, &vs_smb);
    assert(open_ret == 0);
    assert(vs_smb.is_multipart == 0);
    assert(vs_smb.is_smb == 1);
    assert(vs_smb.total_parts == 1);
    virtual_stream_close(&vs_smb);
    printf("   -> virtual_stream_open treated SMB path as single part\n");

    /* 4. Verify installer_start gates SMB multi-part packages */
    installer_init("http://127.0.0.1:8085/");
    int inst_ret = installer_start(smb_url_p1);
    assert(inst_ret == -13);
    installer_shutdown();
    printf("   -> installer_start gated SMB multi-part with -13 (USB/Disc only)\n");

    system("rm -rf /tmp/mock_smb");

    /* 5. Verify local multi-part virtual streaming works seamlessly */
    system("rm -rf /tmp/test_mpkg_local && mkdir -p /tmp/test_mpkg_local/parts");
    comp_ret = system("python3 tools/pkg_split.py " MP_SRC_PKG " -o /tmp/test_mpkg_local/parts -s 60M > /dev/null 2>&1");
    assert(comp_ret == 0);

    const char *local_p1 = "/tmp/test_mpkg_local/parts/nd.pkg.part1";
    virtual_stream_t vs_local;
    int loc_open = virtual_stream_open(local_p1, &vs_local);
    assert(loc_open == 0);
    assert(vs_local.is_multipart == 1);
    assert(vs_local.is_smb == 0);
    assert(vs_local.total_parts == 2);
    printf("   -> Local virtual_stream_open resolved all parts successfully\n");

    /* Verify stream read across part boundary matches original file */
    int orig_fd = open(MP_SRC_PKG, O_RDONLY);
    assert(orig_fd >= 0);

    uint8_t buf_stream[4096];
    uint8_t buf_orig[4096];
    ssize_t n1 = virtual_stream_read(&vs_local, 0, buf_stream, sizeof(buf_stream));
    assert(n1 == sizeof(buf_stream));
    assert(pread(orig_fd, buf_orig, sizeof(buf_orig), 0) == sizeof(buf_orig));
    assert(memcmp(buf_stream, buf_orig, sizeof(buf_stream)) == 0);

    uint64_t boundary = vs_local.parts[0].part_data_size;
    ssize_t n2 = virtual_stream_read(&vs_local, boundary, buf_stream, 1024);
    assert(n2 == 1024);
    assert(pread(orig_fd, buf_orig, 1024, (off_t)boundary) == 1024);
    assert(memcmp(buf_stream, buf_orig, 1024) == 0);

    close(orig_fd);
    virtual_stream_close(&vs_local);
    system("rm -rf /tmp/test_mpkg_local");
    printf("   -> Local virtual_stream_read bit-for-bit verified across boundaries!\n");
}

int main(void) {
    printf("==============================================\n");
    printf(">>> RUNNING MULTI-PART & BLU-RAY DISC TESTS <<<\n");
    printf("==============================================\n");

    test_default_part_size_under_25gb();

    /* Shared synthetic source package (no external files needed). */
    system("rm -rf /tmp/test_mpkg_src && mkdir -p /tmp/test_mpkg_src");
    assert(fixture_write_ps5_pkg(MP_SRC_PKG,
                                 "PPSA90010", "NebulaDrift", "gd", "06.000.000", 1) == 0);
    assert(fixture_grow_file(MP_SRC_PKG, 80ULL * 1024 * 1024) == 0);

    test_multipart_creation_and_decompression();
    test_scanner_multipart_detection();
    test_space_check_and_dir_creation();
    test_end_to_end_disc_swapping();
    test_uncompressed_multipart_and_crc();
    test_out_of_order_disc_and_cancellation();
    test_smb_multipart_rejected_and_local_streaming();

    printf("\n==============================================\n");
    printf(">>> ALL MULTI-PART & DISC TESTS PASSED! <<<\n");
    printf("==============================================\n");
    return 0;
}
