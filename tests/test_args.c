#include "../include/cpdd.h"
#include <assert.h>

static void test_basic_args(void) {
    printf("Testing basic argument parsing...\n");
    
    char *argv[] = {"cpdd", "source", "dest"};
    int argc = 3;
    options_t opts;
    
    assert(parse_args(argc, argv, &opts) == 0);
    assert(strcmp(opts.src_dir, "source") == 0);
    assert(strcmp(opts.dest_dir, "dest") == 0);
    assert(opts.ref_dir == NULL);
    assert(opts.link_type == LINK_NONE);
    assert(opts.verbose == 0);
    assert(opts.recursive == 0);
    
    printf("✓ Basic argument parsing test passed\n");
}

static void test_reference_dir_args(void) {
    printf("Testing reference directory arguments...\n");
    
    char *argv[] = {"cpdd", "-r", "reference", "-H", "source", "dest"};
    int argc = 6;
    options_t opts;
    
    assert(parse_args(argc, argv, &opts) == 0);
    assert(strcmp(opts.src_dir, "source") == 0);
    assert(strcmp(opts.dest_dir, "dest") == 0);
    assert(strcmp(opts.ref_dir, "reference") == 0);
    assert(opts.link_type == LINK_HARD);
    assert(opts.verbose == 0);
    assert(opts.recursive == 0);
    
    printf("✓ Reference directory argument test passed\n");
}

static void test_symbolic_link_args(void) {
    printf("Testing symbolic link arguments...\n");
    
    char *argv[] = {"cpdd", "--reference", "ref", "--symbolic-link", "--verbose", "--recursive", "src", "dst"};
    int argc = 8;
    options_t opts;
    
    assert(parse_args(argc, argv, &opts) == 0);
    assert(strcmp(opts.src_dir, "src") == 0);
    assert(strcmp(opts.dest_dir, "dst") == 0);
    assert(strcmp(opts.ref_dir, "ref") == 0);
    assert(opts.link_type == LINK_SOFT);
    assert(opts.verbose == 1);
    assert(opts.recursive == 1);
    
    printf("✓ Symbolic link argument test passed\n");
}

static void test_conflicting_link_args(void) {
    printf("Testing conflicting link type arguments...\n");
    
    char *argv[] = {"cpdd", "-H", "-s", "source", "dest"};
    int argc = 5;
    options_t opts;
    
    assert(parse_args(argc, argv, &opts) == -1);
    
    printf("✓ Conflicting link argument test passed\n");
}

static void test_missing_args(void) {
    printf("Testing missing required arguments...\n");
    
    char *argv[] = {"cpdd", "source"};
    int argc = 2;
    options_t opts;
    
    assert(parse_args(argc, argv, &opts) == -1);
    
    printf("✓ Missing argument test passed\n");
}

static void test_help_arg(void) {
    printf("Testing help argument...\n");
    
    char *argv[] = {"cpdd", "--help"};
    int argc = 2;
    options_t opts;
    
    assert(parse_args(argc, argv, &opts) == 1);
    
    printf("✓ Help argument test passed\n");
}

int main(void) {
    printf("Running argument parsing test suite...\n\n");
    
    test_basic_args();
    test_reference_dir_args();
    test_symbolic_link_args();
    test_conflicting_link_args();
    test_missing_args();
    test_help_arg();
    
    printf("\n✓ All argument parsing tests passed!\n");
    return 0;
}