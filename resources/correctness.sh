#!/bin/bash

GAME="rand.gm"
EXE="./nocq"
PARAMS="30 50 1 10"

echo "--- Step 0: Generating Game ---"
$EXE --rand $PARAMS --export-gm $GAME

echo "--- step 1: extracting full ground truth (zra) ---"
# store the output
ZRA_FULL_OUT=$($EXE --gm $GAME --zra --print-solution)

# extract the sets
EVEN_SET=$(echo "$ZRA_FULL_OUT" | grep "EVEN {" | sed -n 's/.*{\(.*\)}.*/\1/p' | tr ',' ' ')
ODD_SET=$(echo "$ZRA_FULL_OUT" | grep "ODD  {" | sed -n 's/.*{\(.*\)}.*/\1/p' | tr ',' ' ')

echo "ZRA EVEN: { $EVEN_SET }"
echo "ZRA ODD:  { $ODD_SET }"
echo "--------------------------------------------------"

PASS=0
FAIL=0

# function to run the check
verify_node() {
    local node=$1
    local expected=$2
    echo -n "node $node (expected $expected): "
    
    RESULT=$($EXE --gm $GAME --noc --chuffed-hyperbool --init $node --branch-reach)
    
if [[ "$RESULT" == *"$expected"* ]]; then
        echo "✅ match"
        ((PASS++))
    elif [[ "$RESULT" == *"EVEN"* ]] || [[ "$RESULT" == *"ODD"* ]]; then
        echo "❌ mismatch! result: $RESULT"
        ((FAIL++))
    else
        echo "⚠️  unexpected output / error! result: $RESULT"
        ((OTHER++))
    fi
}

# check all even nodes
for n in $EVEN_SET; do
    verify_node "$n" "EVEN"
done

# check all odd nodes
for n in $ODD_SET; do
    verify_node "$n" "ODD"
done

echo "--------------------------------------------------"
echo "verification complete: $PASS passed, $FAIL failed."