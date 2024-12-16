#!/bin/bash

# Base directory to check
BASE_DIR="/sys/kernel/mm/transparent_hugepage"

# Check if base directory exists
if [ ! -d "$BASE_DIR" ]; then
    echo "Error: $BASE_DIR does not exist."
    exit 1
fi

# Iterate over directories matching the pattern hugepages-<size>kB
for dir in "$BASE_DIR"/hugepages-*kB/; do
    # Ensure it's a directory before proceeding
    if [ -d "$dir" ]; then
        echo "Directory: $dir"

        # Iterate through all files in the directory
        for file in "$dir"*; do
            # Ensure it's a file
            if [ -f "$file" ]; then
                echo "File: $file"
                # Read and display the file content
                cat "$file"
                echo ""  # Add a blank line for better readability
            fi
        done
        echo "----------------------------------------"
    fi
done
