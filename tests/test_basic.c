#include "../include/cpdd.h"
#include <assert.h>
#include <sys/wait.h>

#define TEST_DIR "/tmp/cpdd_test"
#define SRC_DIR TEST_DIR "/src"
#define DEST_DIR TEST_DIR "/dest"
#define REF_DIR TEST_DIR "/ref"

static void cleanup_test_dirs(void) {
    system("rm -rf " TEST_DIR);
}

static void setup_test_dirs(void) {
    cleanup_test_dirs();
    system("mkdir -p " SRC_DIR);
    system("mkdir -p " DEST_DIR);
    system("mkdir -p " REF_DIR);
}

static void create_test_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    fprintf(f, "%s", content);
    fclose(f);
}

static int file_exists(const char *path) {
    return access(path, F_OK) == 0;
}

static int files_have_same_content(const char *file1, const char *file2) {
    FILE *f1 = fopen(file1, "r");
    FILE *f2 = fopen(file2, "r");
    char c1, c2;
    
    if (!f1 || !f2) {
        if (f1) fclose(f1);
        if (f2) fclose(f2);
        return 0;
    }
    
    do {
        c1 = fgetc(f1);
        c2 = fgetc(f2);
        if (c1 != c2) {
            fclose(f1);
            fclose(f2);
            return 0;
        }
    } while (c1 != EOF && c2 != EOF);
    
    fclose(f1);
    fclose(f2);
    return 1;
}

static void test_basic_copy(void) {
    printf("Testing basic file copy...\n");
    
    setup_test_dirs();
    create_test_file(SRC_DIR "/file1.txt", "Hello, World!");
    
    options_t opts = {0};
    opts.src_dir = SRC_DIR "/file1.txt";
    opts.dest_dir = DEST_DIR "/file1.txt";
    opts.link_type = LINK_NONE;
    opts.verbose = 0;
    opts.recursive = 0;
    
    assert(copy_directory(&opts) == 0);
    assert(file_exists(DEST_DIR "/file1.txt"));
    assert(files_have_same_content(SRC_DIR "/file1.txt", DEST_DIR "/file1.txt"));
    
    printf("✓ Basic copy test passed\n");
}

static void test_recursive_copy(void) {
    printf("Testing recursive directory copy...\n");
    
    setup_test_dirs();
    system("mkdir -p " SRC_DIR "/subdir");
    create_test_file(SRC_DIR "/file1.txt", "File 1 content");
    create_test_file(SRC_DIR "/subdir/file2.txt", "File 2 content");
    
    options_t opts = {0};
    opts.src_dir = SRC_DIR;
    opts.dest_dir = DEST_DIR;
    opts.link_type = LINK_NONE;
    opts.verbose = 0;
    opts.recursive = 1;
    
    assert(copy_directory(&opts) == 0);
    assert(file_exists(DEST_DIR "/file1.txt"));
    assert(file_exists(DEST_DIR "/subdir/file2.txt"));
    assert(files_have_same_content(SRC_DIR "/file1.txt", DEST_DIR "/file1.txt"));
    assert(files_have_same_content(SRC_DIR "/subdir/file2.txt", DEST_DIR "/subdir/file2.txt"));
    
    printf("✓ Recursive copy test passed\n");
}

static void test_hard_link_creation(void) {
    printf("Testing hard link creation...\n");
    
    setup_test_dirs();
    create_test_file(SRC_DIR "/file1.txt", "Identical content");
    create_test_file(REF_DIR "/reference.txt", "Identical content");
    
    options_t opts = {0};
    opts.src_dir = SRC_DIR "/file1.txt";
    opts.dest_dir = DEST_DIR "/file1.txt";
    opts.ref_dir = REF_DIR;
    opts.link_type = LINK_HARD;
    opts.verbose = 1;
    opts.recursive = 0;
    
    assert(copy_directory(&opts) == 0);
    assert(file_exists(DEST_DIR "/file1.txt"));
    
    struct stat ref_stat, dest_stat;
    assert(stat(REF_DIR "/reference.txt", &ref_stat) == 0);
    assert(stat(DEST_DIR "/file1.txt", &dest_stat) == 0);
    
    assert(ref_stat.st_ino == dest_stat.st_ino);
    assert(ref_stat.st_nlink > 1);
    
    printf("✓ Hard link creation test passed\n");
}

static void test_soft_link_creation(void) {
    printf("Testing symbolic link creation...\n");
    
    setup_test_dirs();
    create_test_file(SRC_DIR "/file1.txt", "Identical content");
    create_test_file(REF_DIR "/reference.txt", "Identical content");
    
    options_t opts = {0};
    opts.src_dir = SRC_DIR "/file1.txt";
    opts.dest_dir = DEST_DIR "/file1.txt";
    opts.ref_dir = REF_DIR;
    opts.link_type = LINK_SOFT;
    opts.verbose = 1;
    opts.recursive = 0;
    
    assert(copy_directory(&opts) == 0);
    assert(file_exists(DEST_DIR "/file1.txt"));
    
    struct stat dest_stat;
    assert(lstat(DEST_DIR "/file1.txt", &dest_stat) == 0);
    assert(S_ISLNK(dest_stat.st_mode));
    
    printf("✓ Symbolic link creation test passed\n");
}

static void test_md5_matching(void) {
    printf("Testing MD5-based file matching...\n");
    
    unsigned char md5_1[MD5_DIGEST_LENGTH];
    unsigned char md5_2[MD5_DIGEST_LENGTH];
    unsigned char md5_3[MD5_DIGEST_LENGTH];
    
    setup_test_dirs();
    create_test_file(SRC_DIR "/file1.txt", "Same content");
    create_test_file(REF_DIR "/file2.txt", "Same content");
    create_test_file(REF_DIR "/file3.txt", "Different content");
    
    assert(calculate_md5(SRC_DIR "/file1.txt", md5_1) == 0);
    assert(calculate_md5(REF_DIR "/file2.txt", md5_2) == 0);
    assert(calculate_md5(REF_DIR "/file3.txt", md5_3) == 0);
    
    assert(memcmp(md5_1, md5_2, MD5_DIGEST_LENGTH) == 0);
    assert(memcmp(md5_1, md5_3, MD5_DIGEST_LENGTH) != 0);
    
    printf("✓ MD5 matching test passed\n");
}

static void test_content_comparison(void) {
    printf("Testing content comparison...\n");
    
    setup_test_dirs();
    create_test_file(SRC_DIR "/file1.txt", "Identical content for comparison");
    create_test_file(REF_DIR "/file2.txt", "Identical content for comparison");
    create_test_file(REF_DIR "/file3.txt", "Different content for comparison");
    
    assert(files_identical(SRC_DIR "/file1.txt", REF_DIR "/file2.txt") == 1);
    assert(files_identical(SRC_DIR "/file1.txt", REF_DIR "/file3.txt") == 0);
    
    printf("✓ Content comparison test passed\n");
}

int main(void) {
    printf("Running cpdd test suite...\n\n");
    
    test_basic_copy();
    test_recursive_copy();
    test_hard_link_creation();
    test_soft_link_creation();
    test_md5_matching();
    test_content_comparison();
    
    cleanup_test_dirs();
    
    printf("\n✓ All tests passed!\n");
    return 0;
}