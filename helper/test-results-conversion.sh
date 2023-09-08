#!/bin/bash

# Assuming results.txt contains your test results
input="results"

pass_count=0
fail_count=0
total_count=0

# Output File
output="test_results.md"

# Empty the output file
> $output

echo "### Test Results" >> $output
echo "" >> $output

# Markdown table headers
echo "| Test Case | Results |" >> $output
echo "|-----------|--------|" >> $output

while IFS= read -r line
do
    total_count=$((total_count+1))

    if [[ $line == *'pass'* ]]; then
        pass_count=$((pass_count+1))
        # Extract test name using awk
        test_name=$(echo $line | awk '{print $2}')
        echo "| $test_name | :white_check_mark: |" >> $output
    elif [[ $line == *'FAIL'* ]]; then
        fail_count=$((fail_count+1))
        test_name=$(echo $line | awk '{print $2}')
        echo "| $test_name | :x: |" >> $output
    else
        echo "| $line | - |" >> $output
    fi
done < "$input"

echo "" >> $output
echo "### Summary" >> $output
echo "" >> $output
echo "Total Tests: $total_count" >> $output
echo "Passed: $pass_count" >> $output
echo "Failed: $fail_count" >> $output

coverage=$(( (pass_count * 100) / total_count ))
echo "Coverage: $coverage%" >> $output
