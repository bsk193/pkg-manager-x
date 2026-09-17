#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
#include "leftovers.h"
#include "app_info.h"

int main(void) {
    printf("=== Testing Leftover Scanning & Cleanup ===\n");

    system("rm -rf /tmp/test_leftovers_meta /tmp/test_leftovers_patch /tmp/test_leftovers_addcont /tmp/test_leftovers_user_app");
    system("mkdir -p /tmp/test_leftovers_meta /tmp/test_leftovers_patch /tmp/test_leftovers_addcont /tmp/test_leftovers_user_app");

    setenv("PKG_APPMETA_DIR", "/tmp/test_leftovers_meta", 1);
    setenv("PKG_USER_PATCH_DIR", "/tmp/test_leftovers_patch", 1);
    setenv("PKG_ADDCONT_DIR", "/tmp/test_leftovers_addcont", 1);
    setenv("PKG_USER_APP_DIR", "/tmp/test_leftovers_user_app", 1);
    unsetenv("PKG_APP_DB_PATH");

    /* 1. Empty environment: scan should return count 0 */
    char *json = leftovers_scan_json();
    assert(json != NULL);
    printf("Empty scan JSON: %s\n", json);
    assert(strstr(json, "\"count\":0") != NULL);
    free(json);

    /* 2. Create orphaned patch for CUSA90001 (orphaned patch scenario) */
    system("mkdir -p /tmp/test_leftovers_meta/CUSA90001");
    FILE *f_meta = fopen("/tmp/test_leftovers_meta/CUSA90001/param.json", "w");
    assert(f_meta != NULL);
    fprintf(f_meta, "{\"titleId\":\"CUSA90001\",\"contentVersion\":\"01.260.000\",\"titleName\":\"BlockBuddies 3\"}\n");
    fclose(f_meta);

    system("mkdir -p /tmp/test_leftovers_patch/CUSA90001/sce_sys");
    FILE *f_dummy = fopen("/tmp/test_leftovers_patch/CUSA90001/patch_data.bin", "w");
    assert(f_dummy != NULL);
    /* Write 1000 bytes */
    char dummy[1000] = {0};
    fwrite(dummy, 1, sizeof(dummy), f_dummy);
    fclose(f_dummy);

    /* Also create an empty folder in user_app for CUSA90001 */
    system("mkdir -p /tmp/test_leftovers_user_app/CUSA90001");

    /* 3. Create a legitimately installed package CUSA99999 (should NOT appear in leftovers) */
    system("mkdir -p /tmp/test_leftovers_user_app/CUSA99999/sce_sys /tmp/test_leftovers_meta/CUSA99999 /tmp/test_leftovers_patch/CUSA99999");
    FILE *f_legit_app = fopen("/tmp/test_leftovers_user_app/CUSA99999/sce_sys/param.json", "w");
    assert(f_legit_app != NULL);
    fprintf(f_legit_app, "{\"titleId\":\"CUSA99999\",\"contentVersion\":\"01.000.000\",\"titleName\":\"Legit Application\"}\n");
    fclose(f_legit_app);

    FILE *f_legit_meta = fopen("/tmp/test_leftovers_meta/CUSA99999/param.json", "w");
    assert(f_legit_meta != NULL);
    fprintf(f_legit_meta, "{\"titleId\":\"CUSA99999\",\"contentVersion\":\"01.000.000\",\"titleName\":\"Legit Application\"}\n");
    fclose(f_legit_meta);

    /* Scan now */
    json = leftovers_scan_json();
    assert(json != NULL);
    printf("Leftovers scan JSON with 1 orphan and 1 legit app:\n%s\n", json);
    fflush(stdout);
    assert(strstr(json, "\"count\":1") != NULL);
    assert(strstr(json, "CUSA90001") != NULL);
    assert(strstr(json, "BlockBuddies 3") != NULL);
    assert(strstr(json, "Orphaned Update") != NULL);
    /* Legit app must NOT be in leftovers */
    assert(strstr(json, "CUSA99999") == NULL);
    free(json);

    /* 4. Guard test: Try deleting legitimately installed package -> MUST FAIL */
    char *del_legit = leftovers_delete_json("CUSA99999");
    assert(del_legit != NULL);
    printf("Delete legit app response: %s\n", del_legit);
    assert(strstr(del_legit, "\"success\":false") != NULL);
    assert(strstr(del_legit, "Base package is currently installed. Cannot delete!") != NULL);
    free(del_legit);

    /* 5. Delete orphaned leftovers for CUSA90001 */
    char *del_orphan = leftovers_delete_json("CUSA90001");
    assert(del_orphan != NULL);
    printf("Delete orphan response: %s\n", del_orphan);
    assert(strstr(del_orphan, "\"success\":true") != NULL);
    assert(strstr(del_orphan, "\"freed_bytes\":") != NULL);
    free(del_orphan);

    /* Verify files were deleted on disk */
    struct stat st;
    assert(stat("/tmp/test_leftovers_patch/CUSA90001", &st) != 0);
    assert(stat("/tmp/test_leftovers_meta/CUSA90001", &st) != 0);
    assert(stat("/tmp/test_leftovers_user_app/CUSA90001", &st) != 0);
    /* Legit app files must still be intact! */
    assert(stat("/tmp/test_leftovers_user_app/CUSA99999/sce_sys/param.json", &st) == 0);

    /* 6. Scan again: count should now be 0 */
    json = leftovers_scan_json();
    assert(json != NULL);
    printf("Post-delete scan JSON: %s\n", json);
    assert(strstr(json, "\"count\":0") != NULL);
    free(json);

    system("rm -rf /tmp/test_leftovers_meta /tmp/test_leftovers_patch /tmp/test_leftovers_addcont /tmp/test_leftovers_user_app");
    unsetenv("PKG_APPMETA_DIR");
    unsetenv("PKG_USER_PATCH_DIR");
    unsetenv("PKG_ADDCONT_DIR");
    unsetenv("PKG_USER_APP_DIR");

    printf("\n>>> ALL LEFTOVERS TESTS PASSED! <<<\n");
    return 0;
}
