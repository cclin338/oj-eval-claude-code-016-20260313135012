#!/usr/bin/env python3

# Generate test for same index with many values
n = 200
print(n)
for i in range(1, 101):
    print(f"insert key1 {i}")
for i in range(1, 101):
    if i % 2 == 0:
        print(f"delete key1 {i}")
print("find key1")
