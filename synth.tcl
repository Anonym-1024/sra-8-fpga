
# Tip: You can also use wildcards if supported by your shell:

read_verilog *.v

# 2. Elaborate hierarchy and set your top module
hierarchy -top CPU
stat

# 3. Convert behavioral processes (always blocks, if/else, case) to netlists
proc
#
# 4. (Optional) Prevent dead-code sweeping across ALL wires
#setattr -set keep 1 w:*

# 5. Extract multi-bit registers and memories
#memory

# 6. Flatten hierarchy (optional, useful for accurate global LUT counts)
#flatten

# 7. Map arithmetic and boolean logic to generic gates
#techmap

# 8. Map generic gates specifically to iCE40 architecture primitives (SB_LUT4, SB_CARRY)
#abc -lut 4
#techmap -map +/ice40/cells_sim.v

# 9. Clean up temporary wires/nodes
#clean

# 10. Print the final resource usage report
#stat
#