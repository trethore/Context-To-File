#include "ctf.h"


char *content_types[MAX_CONTENT_TYPES];
int content_type_count = 0;

char *addf_dirs[MAX_ADDF];
int addf_count = 0;

char *rmf_dirs[MAX_RMF];
int rmf_count = 0;
char gitignore_entries[MAX_RMF][MAX_PATH_SIZE];

int content_flag = 0;
int git_flag = 0;
FILE *output = NULL;

char output_dir[MAX_PATH_SIZE] = ".";
char output_name[MAX_PATH_SIZE] = "";

// conpiled file extensions add more to your liking
const char *compiled_exts[] = {"exe", "bin", "o", "obj", "class", NULL};

// file name exceptions add more to your liking
const char *content_exceptions[] = {"Dockerfile", NULL};

int main(int argc, char *argv[]) {
    parse_arguments(argc, argv);


    if (git_flag) {
        load_gitignore();
    }

    char *filename = get_output_filename();
    if (!filename) {
        fprintf(stderr, "Failed to allocate output filename.\n");
        return 1;
    }
    output = fopen(filename, "w");
    if (!output) {
        perror("fopen");
        free(filename);
        return 1;
    }

    int traversed = 0;
    for (int i = 0; i < addf_count; i++) {
        int skip = 0;
        for (int j = 0; j < rmf_count; j++) {


            if (strcmp(addf_dirs[i], rmf_dirs[j]) == 0) {
                skip = 1;
                break;
            }
        }
        if (skip)
            continue;

        int depth = print_path_hierarchy(addf_dirs[i]);
        traverse_directory(addf_dirs[i], depth);
        traversed = 1;
    }

    fclose(output);
    if (traversed)
        printf("Output written to %s\n", filename);
    else {
        printf("No directories traversed or all were excluded.\n");

        remove(filename);
    }

    free(filename);
    return 0;
}
