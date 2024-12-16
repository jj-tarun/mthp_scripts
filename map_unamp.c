#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>

#define PMD_SIZE (2048 * 1024)
#define STEP_SIZE (256 * 1024)
#define NUM_PAGES 4

// Function to write a string to a file
int write_to_file(const char *path, const char *content) {
    FILE *file = fopen(path, "w");
    if (!file) {
        perror("fopen");
        return -1;
    }

    if (fprintf(file, "%s", content) < 0) {
        perror("fprintf");
        fclose(file);
        return -1;
    }

    fclose(file);
    return 0;
}

// Function to read the content of a file into a string
char *read_file(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) {
        perror("fopen");
        return NULL;
    }

    static char buffer[256];
    if (!fgets(buffer, sizeof(buffer), file)) {
        perror("fgets");
        fclose(file);
        return NULL;
    }

    fclose(file);
    return buffer;
}

// Function to read the content of a file as a long value
long read_stat(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) {
        perror("fopen");
        return -1;
    }

    long value;
    if (fscanf(file, "%ld", &value) != 1) {
        perror("fscanf");
        fclose(file);
        return -1;
    }

    fclose(file);
    return value;
}

// Custom formula to generate a unique value based on the offset
char calculate_value(size_t offset) {
    return (char)((offset / STEP_SIZE) % 256);
}

// Function to enable all hugepage sizes up to the given page_size
void enable_hugepages_sizes(size_t page_size, char *enable_str) {
    // Enable hugepages up to the specified size, iterating through sizes up to 2048 kB
    size_t sizes[] = {32768, 16384, 8192, 4096, 2048};  // Sizes in kB
    for (size_t i = 0; i < sizeof(sizes)/sizeof(sizes[0]); ++i) {
        if (sizes[i] <= page_size) {
            char path[256];
            snprintf(path, sizeof(path), "/sys/kernel/mm/transparent_hugepage/hugepages-%zukB/enabled", sizes[i]);
            write_to_file(path, enable_str);
        }
    }
}

// Function to read and dump stats for each hugepage size
void read_and_dump_stats(size_t page_size) {
    // Read stats for each hugepage size from 2048 down to the specified size
    size_t sizes[] = {32768, 16384, 8192, 4096, 2048};  // Sizes in kB
    for (size_t i = 0; i < sizeof(sizes)/sizeof(sizes[0]); ++i) {
        if (sizes[i] <= page_size) {
            char path[256], path2[256];
            snprintf(path, sizeof(path), "/sys/kernel/mm/transparent_hugepage/hugepages-%zukB/stats/nr_anon", sizes[i]);
            snprintf(path2, sizeof(path2), "/sys/kernel/mm/transparent_hugepage/hugepages-%zukB/stats/anon_fault_alloc", sizes[i]);
            long stat = read_stat(path);
            long stat2 = read_stat(path2);
            if (stat != -1) {
                printf("Hugepage size %zu kB, nr_anon: %ld\n", sizes[i], stat);
            }
            if (stat2 != -1) {
                printf("Hugepage size %zu kB, anon_fault_alloc: %ld\n", sizes[i], stat2);
            }
        }
    }
}

// Function to touch memory and verify contents
void touch_and_verify_memory(void *ptr, size_t page_size) {
    volatile char *p = (volatile char *)ptr;
    size_t total_size = NUM_PAGES * page_size;

    // Touch memory with values derived from the custom formula
    for (size_t offset = 0; offset < total_size; offset += STEP_SIZE) {
        p[offset] = calculate_value(offset); // Assign a unique char value for each offset
    }

    // Verify the memory contents
    for (size_t offset = 0; offset < total_size; offset += STEP_SIZE) {
        if (p[offset] != calculate_value(offset)) {
            fprintf(stderr, "Verification failed at offset %zu\n", offset);
            exit(EXIT_FAILURE);
        }
    }

    printf("Memory touched and verified successfully.\n");
}

// Function to partially unmap memory and verify the remaining content
void partial_unmap_and_verify(void *ptr, size_t page_size) {
    volatile char *p = (volatile char *)ptr;
    size_t total_size = NUM_PAGES * page_size;
    int num_pmd_thps = NUM_PAGES * (page_size / PMD_SIZE);

    // Unmap specific regions and verify the remaining memory
    for (size_t page = 0; page < num_pmd_thps; ++page) {
        size_t page_start = page * PMD_SIZE;
        size_t unmap_offset = page_start + ((page * STEP_SIZE) % PMD_SIZE);

        if (munmap((void *)(p + unmap_offset), STEP_SIZE) == -1) {
            perror("munmap partial");
            exit(EXIT_FAILURE);
        }

        printf("Unmapped region: %zu to %zu\n", unmap_offset, unmap_offset + STEP_SIZE);
    }

    // Verify the remaining mapped memory
    for (size_t offset = 0; offset < total_size; offset += STEP_SIZE) {
        if (msync((void *)(p + offset), page_size, MS_ASYNC) == -1) {
            fprintf(stderr, "Region already unmapped at offset: %zu\n", offset);
        } else {
            if (p[offset] != calculate_value(offset)) {
                fprintf(stderr, "FAILED at offset %zu, expected: %d but found: %d\n", offset, calculate_value(offset), p[offset]);
                exit(EXIT_FAILURE);
            }
        }
    }

    printf("Remaining memory verified successfully after partial unmap.\n");
}

int main(int argc, char *argv[]) {
	char enable_str[20] = "always";
	const char *allowed_values[] = {"always", "madvise", "inherit", "never"};
    int num_allowed_values = sizeof(allowed_values) / sizeof(allowed_values[0]);

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <page_size_in_kB>\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (argc > 2) {
      const char *input = argv[2];
      int valid = 0;

      // Check if input matches one of the allowed values
      for (int i = 0; i < num_allowed_values; i++) {
        if (strcmp(input, allowed_values[i]) == 0) {
          valid = 1;
          break;
        }
      }

      if (valid) {
        // Set enable_str to input
        strncpy(enable_str, input, sizeof(enable_str) - 1);
        enable_str[sizeof(enable_str) - 1] = '\0';  // Ensure null termination
        printf("enable_str set to: %s\n", enable_str);
      } else {
        printf("Invalid value provided: %s. Allowed values are:\n", input);
        for (int i = 0; i < num_allowed_values; i++) {
          printf(" - %s\n", allowed_values[i]);
        }
        exit(1);
      }
    } else {
      printf("No second argument provided. Using default value: %s\n",
             enable_str);
    }

    // Parse the page size argument
    size_t page_size_kb = strtoul(argv[1], NULL, 10);
    if (page_size_kb == 0) {
        fprintf(stderr, "Invalid page size: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    size_t page_size = page_size_kb * 1024; // Convert to bytes

    // Enable hugepages of appropriate sizes
    enable_hugepages_sizes(page_size_kb, enable_str);

    // Map memory for the given size
    void *memory = mmap(NULL, NUM_PAGES * page_size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) {
        perror("mmap");
        return EXIT_FAILURE;
    }

	madvise(memory, NUM_PAGES * page_size, MADV_HUGEPAGE);
    printf("Memory mapped successfully.\n");

    // Read and dump stats for the enabled hugepages
    printf("Before Fault......\n");
    read_and_dump_stats(page_size_kb);

    // Touch and verify memory
    touch_and_verify_memory(memory, page_size);

    printf("After Fault......\n");
    read_and_dump_stats(page_size);
    partial_unmap_and_verify(memory, page_size);

    printf("After partial unamp......\n");
    read_and_dump_stats(page_size);

    // Unmap remaining memory
    if (munmap(memory, NUM_PAGES * page_size) == -1) {
        perror("munmap remaining");
        return EXIT_FAILURE;
    }

    printf("After fully unamp......\n");
    read_and_dump_stats(page_size);
    printf("All memory unmapped successfully.\n");

    return EXIT_SUCCESS;
}
