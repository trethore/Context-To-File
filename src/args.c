#define _POSIX_C_SOURCE 200809L
#include "recap.h"
#include <curl/curl.h>
#include <unistd.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <getopt.h>
#include <limits.h>
/**
 * Helper to add comma-separated content specifiers from a string to the content_ctx.
 */
static void add_content_specifiers(const char* arg, content_ctx* ctx) {
    char* arg_copy = strdup(arg);
    if (!arg_copy) {
        fprintf(stderr, "Memory allocation error\n");
        exit(1);
    }
    char* save_ptr = NULL;
    char* token = strtok_r(arg_copy, ",", &save_ptr);
    while (token != NULL) {
        if (ctx->content_specifier_count < MAX_CONTENT_SPECIFIERS) {
            ctx->content_specifiers[ctx->content_specifier_count++] = strdup(token);
        }
        else {
            fprintf(stderr, "Too many content specifiers. Max allowed is %d\n", MAX_CONTENT_SPECIFIERS);
            free(arg_copy);
            exit(1);
        }
        token = strtok_r(NULL, ",", &save_ptr);
    }
    free(arg_copy);
}

// ptn de warning de merde, WSL warning pls dont put too much brain power into this next line
char* realpath(const char* restrict path, char* restrict resolved_path);

void clear_recap_output_files(const char* target_dir) {
    char full_path[MAX_PATH_SIZE];
    if (!target_dir || strlen(target_dir) == 0) {
        target_dir = ".";
    }

    if (realpath(target_dir, full_path) == NULL) {
        strncpy(full_path, target_dir, MAX_PATH_SIZE - 1);
        full_path[MAX_PATH_SIZE - 1] = '\0';
    }


    printf("Warning: This will delete every file matching 'recap-output*' in '%s'.\n", full_path);
    printf("Are you sure? (y/N): ");
    char confirmation;
    if (scanf(" %c", &confirmation) != 1 || tolower(confirmation) != 'y') {
        printf("Operation cancelled.\n");
        return;
    }

    DIR* dir = opendir(target_dir);
    if (!dir) {
        perror("opendir for clearing");
        fprintf(stderr, "Failed to open directory: %s\n", target_dir);
        return;
    }

    struct dirent* entry;
    int deleted_count = 0;
    char file_to_remove[MAX_PATH_SIZE];

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "recap-output", strlen("recap-output")) == 0) {
            snprintf(file_to_remove, sizeof(file_to_remove), "%s/%s", target_dir, entry->d_name);
            if (remove(file_to_remove) == 0) {
                printf("Deleted: %s\n", file_to_remove);
                deleted_count++;
            }
            else {
                perror("remove recap-output file");
                fprintf(stderr, "Failed to delete: %s\n", file_to_remove);
            }
        }
    }
    closedir(dir);
    if (deleted_count > 0) {
        printf("Cleared %d recap-output file(s) from %s.\n", deleted_count, full_path);
    }
    else {
        printf("No recap-output files found to clear in %s.\n", full_path);
    }
}

void load_gitignore(exclude_patterns_ctx* exclude_ctx, const char* gitignore_filename) {
    // Recursively search for the gitignore file up to root
    char cwd[MAX_PATH_SIZE];
    if (!getcwd(cwd, sizeof(cwd))) {
        perror("getcwd");
        return;
    }

    char search_path[MAX_PATH_SIZE];
    const char* filename = gitignore_filename && strlen(gitignore_filename) > 0 ? gitignore_filename : ".gitignore";
    int found = 0;

    // Start from cwd, go up to root
    char* dir = strdup(cwd);
    while (dir && strlen(dir) > 0) {
        snprintf(search_path, sizeof(search_path), "%s/%s", dir, filename);
        FILE* git_ignore_file = fopen(search_path, "r");
        if (git_ignore_file) {
            found = 1;
            int gitignore_idx = 0;
            char line[MAX_PATH_SIZE];
            while (fgets(line, sizeof(line), git_ignore_file)) {
                line[strcspn(line, "\r\n")] = 0;
                char* trimmed_line = line;
                while (isspace((unsigned char)*trimmed_line))
                    trimmed_line++;
                if (trimmed_line[0] == '\0' || trimmed_line[0] == '#') {
                    continue;
                }
                // Support negation patterns (!), comments, blank lines
                if (exclude_ctx->exclude_count < MAX_PATTERNS && gitignore_idx < MAX_GITIGNORE_ENTRIES) {
                    strncpy(exclude_ctx->gitignore_entries[gitignore_idx], trimmed_line, MAX_PATH_SIZE - 1);
                    exclude_ctx->gitignore_entries[gitignore_idx][MAX_PATH_SIZE - 1] = '\0';
                    exclude_ctx->exclude_patterns[exclude_ctx->exclude_count++] = exclude_ctx->gitignore_entries[gitignore_idx];
                    gitignore_idx++;
                }
                else {
                    if (exclude_ctx->exclude_count >= MAX_PATTERNS) {
                        fprintf(stderr, "Warning: Maximum number of exclude patterns (%d) reached while reading %s.\n", MAX_PATTERNS, filename);
                    }
                    break;
                }
            }
            fclose(git_ignore_file);
            break; // Only load the first found .gitignore up the tree
        }
        // Move up one directory
        char* last_slash = strrchr(dir, '/');
        if (last_slash && last_slash != dir) {
            *last_slash = '\0';
        }
        else if (last_slash && last_slash == dir) {
            // At root "/"
            dir[1] = '\0';
        }
        else {
            break;
        }
    }
    free(dir);
    if (!found) {
        // No .gitignore found
        // fprintf(stderr, "No %s found in current or parent directories.\n", filename);
    }
}

void print_help() {
    printf("Usage: recap [options] <include-path>\n");
    printf("Options:\n");
    printf("  --help, -h            Show this help message and exit\n");
    printf("  --version, -v         Show version information and exit\n");
    printf("  --clear, -C [DIR]     Remove previous recap-output files (optionally from DIR)\n");
    printf("  --content, -c [exts]  Include content of files with given extensions\n");
    printf("  --include, -i PATH    Include specific file or directory (repeatable)\n");
    printf("  --exclude, -e PATH    Exclude specific file or directory (repeatable)\n");
    printf("  --git, -g [FILE]      Use .gitignore (or custom FILE) for exclusions; searches parent directories recursively\n");
    printf("  --paste, -p [API_KEY] Upload output as GitHub Gist (API key optional, can also use GITHUB_API_KEY env variable)\n");
    printf("  --output, -o FILE     Write the generated text to the specified file\n");
    printf("  --out-dir, -O DIR     Write the generated text to a timestamped file within the specified directory\n");
    printf("\n");
    printf("The <include-path> positional argument is required unless -i is used. You may specify the include path either as a positional argument or with -i.\n");
    printf("\nExamples:\n");
    printf("  ./recap src\n");
    printf("  ./recap -i src\n");
    printf("  ./recap --output out.txt src\n");
    printf("  ./recap --version\n");
    printf("  ./recap --output somedir/out.txt\n");
    printf("  ./recap --out-dir .\n");
    printf("  ./recap --out-dir somedir/\n");
    printf("  ./recap -i src -e build -c c,h\n");
}


void parse_arguments(int argc, char* argv[],
    include_patterns_ctx* include_ctx,
    exclude_patterns_ctx* exclude_ctx,
    output_ctx* output_context,
    content_ctx* content_context,
    char** gist_api_key,
    const char* version) {
    include_ctx->include_count = 0;
    exclude_ctx->exclude_count = 0;
    content_context->content_specifier_count = 0;
    content_context->content_flag = 0;
    *gist_api_key = NULL;

    static struct option long_options[] = {
        {"help",       no_argument,       0, 'h'},
        {"version",    no_argument,       0, 'v'},
        {"clear",      optional_argument, 0, 'C'},
        {"content",    required_argument, 0, 'c'},
        {"include",    required_argument, 0, 'i'},
        {"exclude",    required_argument, 0, 'e'},
        {"git",        optional_argument, 0, 'g'},
        {"paste",      optional_argument, 0, 'p'},
        {"output",     required_argument, 0, 'o'},
        {"output-dir", required_argument, 0, 'O'},
        {0, 0, 0, 0}
    };

    int opt, option_index = 0;
    while ((opt = getopt_long(argc, argv, "hC::c:i:e:g::p:o:O:v", long_options, &option_index)) != -1) {
        switch (opt) {
        case 'h':
            print_help();
            exit(0);

        case 'C':
            if (!optarg && optind < argc && argv[optind][0] != '-') {
                optarg = argv[optind++];
            }

            if (optarg) {
                clear_recap_output_files(optarg);
            }
            else {
                clear_recap_output_files(".");
            }
            exit(0);

        case 'c': {
            content_context->content_flag = 1;

            // Add specifiers from the initial optarg
            add_content_specifiers(optarg, content_context);

            // Add subsequent non-flag arguments as additional specifiers
            while (optind < argc && argv[optind][0] != '-') {
                add_content_specifiers(argv[optind], content_context);
                optind++;
            }
            break;
        }

        case 'i':
            if (include_ctx->include_count < MAX_PATTERNS) {
                include_ctx->include_patterns[include_ctx->include_count++] = optarg;
            }
            else {
                fprintf(stderr, "Too many include patterns specified. Max allowed is %d\n", MAX_PATTERNS);
                exit(1);
            }
            break;

        case 'e':
            if (exclude_ctx->exclude_count < MAX_PATTERNS) {
                exclude_ctx->exclude_patterns[exclude_ctx->exclude_count++] = optarg;
            }
            else {
                fprintf(stderr, "Too many exclude patterns specified. Max allowed is %d\n", MAX_PATTERNS);
                exit(1);
            }
            break;

        case 'g':
            if (!optarg && optind < argc && argv[optind][0] != '-') {
                optarg = argv[optind++];
            }
            load_gitignore(exclude_ctx, optarg);
            break;

        case 'p':
            if (optarg) {
                *gist_api_key = optarg;
            }
            else {
                char* env_key = getenv("GITHUB_API_KEY");
                if (env_key && strlen(env_key) > 0) {
                    *gist_api_key = env_key;
                }
                else {
                    *gist_api_key = NULL;
                }
            }
            break;

        case 'o':
            strncpy(output_context->output_name, optarg, MAX_PATH_SIZE - 1);
            output_context->output_name[MAX_PATH_SIZE - 1] = '\0';
            break;

        case 'O':
            strncpy(output_context->output_dir, optarg, MAX_PATH_SIZE - 1);
            output_context->output_dir[MAX_PATH_SIZE - 1] = '\0';
            break;
        case 'v':
            printf("recap version %s\n", version);
            exit(0);
        case '?':
        default:
            print_help();
            exit(1);
        }
    }

    // If no -i/--include was given, but a positional argument remains, treat it as the include path
    if (include_ctx->include_count == 0 && optind < argc) {
        include_ctx->include_patterns[include_ctx->include_count++] = argv[optind++];
    }

    // If still no include path, error
    if (include_ctx->include_count == 0) {
        fprintf(stderr, "Error: No include path specified. You must provide an include path as a positional argument or with -i.\n");
        print_help();
        exit(1);
    }
}