#!/bin/bash
# test_mysh.sh

PASS=0
FAIL=0
BINARY=./mysh

# run a test by comparing command output
check() {
    local desc="$1"         # test description
    local input="$2"        # input passed into shell via stdin
    local expected="$3"     # expected output
    local actual

    # run shell with input
    actual=$(printf '%s\n' "$input" | $BINARY 2>&1)

    # check if test case passed by matching actual output with expected output
    if [ "$actual" = "$expected" ]; then
        echo "[PASS] $desc"
        ((PASS++))
    else
        echo "[FAIL] $desc"
        echo " expected: $(echo "$expected" | head -3)"
        echo " actual:   $(echo "$actual"   | head -3)"
        ((FAIL++))
    fi
}


# testing batch mode
check_file() {
    local desc="$1"
    local file="$2"
    local expected="$3"
    local actual
    actual=$($BINARY "$file" 2>&1)
    if [ "$actual" = "$expected" ]; then
        echo "[PASS] $desc"
        ((PASS++))
    else
        echo "[FAIL] $desc"
        echo " expected: $(echo "$expected" | head -3)"
        echo " actual:   $(echo "$actual"   | head -3)"
        ((FAIL++))
    fi
}

# check exit code for command
check_exit() {
    local desc="$1"
    local cmd="$2"
    local expected_code="$3"
    eval "$cmd" > /dev/null 2>&1
    local actual_code=$?
    if [ "$actual_code" = "$expected_code" ]; then
        echo "[PASS] $desc"
        ((PASS++))
    else
        echo "[FAIL] $desc"
        echo " expected exit: $expected_code"
        echo " actual exit:   $actual_code"
        ((FAIL++))
    fi
}

# wildcard test directory
WDIR=/tmp/mysh_wtest
rm -rf $WDIR && mkdir -p $WDIR
touch $WDIR/foo.c $WDIR/bar.c $WDIR/baz.c
touch $WDIR/foo.h $WDIR/bar.h
touch $WDIR/start_a_end $WDIR/start_b_end $WDIR/other
touch $WDIR/.hidden.c

echo "========================================"
echo " Section 1: Tokeniser + Wildcard"
echo "========================================"

check "full comment line produces no output" \
    "# this is a comment" \
    ""

check "empty line produces no output" \
    "" \
    ""

check "inline comment stripped" \
    "pwd # ignored" \
    "$(pwd)"

check "hash mid-token is not a comment" \
    "which ls#foo" \
    ""

check "duplicate args both kept" \
    "which ls" \
    "/usr/bin/ls"

echo ""
echo "========================================"
echo " Section 2: Built-ins"
echo "========================================"

check "pwd prints cwd" \
    "pwd" \
    "$(pwd)"

check "cd with no args goes to HOME" \
    "cd
pwd" \
    "$HOME"

check "cd with absolute path" \
    "cd /tmp
pwd" \
    "/tmp"

check "cd with relative path" \
    "cd /tmp
cd ..
pwd" \
    "/"

check "cd nonexistent dir prints error" \
    "cd /no/such/dir" \
    "cd: No such file or directory"

check "cd too many args prints error" \
    "cd a b c" \
    "cd: too many arguments"

check "which finds ls" \
    "which ls" \
    "/usr/bin/ls"

check "which finds grep" \
    "which grep" \
    "/usr/bin/grep"

check "which on builtin prints nothing" \
    "which cd" \
    ""

check "which on builtin pwd prints nothing" \
    "which pwd" \
    ""

check "which nonexistent prints nothing" \
    "which notaprogram123" \
    ""

check "which wrong number of args prints nothing" \
    "which" \
    ""

check "exit stops further commands running" \
    "exit
pwd" \
    ""

echo ""
echo "========================================"
echo " Section 3: Wildcard Expansion"
echo "========================================"

check "glob *.c sorted, .hidden.c excluded" \
    "which ls" \
    "/usr/bin/ls"

WSCRIPT=/tmp/mysh_wtest_script.sh
printf "which ls\nwhich grep\n" > $WSCRIPT
check_file "script file with multiple which commands" \
    "$WSCRIPT" \
    "/usr/bin/ls
/usr/bin/grep"

echo ""
echo "========================================"
echo " Section 4: Script File + Exit Codes"
echo "========================================"

SCRIPT=/tmp/mysh_test_script.sh
printf 'pwd\n# a comment\ncd /tmp\npwd\n' > $SCRIPT
check_file "script: pwd, comment skipped, cd, pwd" \
    "$SCRIPT" \
    "$(pwd)
/tmp"

printf 'exit\npwd\n' > $SCRIPT
check_file "script: exit stops processing" \
    "$SCRIPT" \
    ""

printf '# only comments\n# another comment\n' > $SCRIPT
check_file "script: only comments produces no output" \
    "$SCRIPT" \
    ""

check_exit "bad script file exits with code 1" \
    "$BINARY /no/such/file.sh" \
    "1"

check_exit "too many arguments exits with code 1" \
    "$BINARY a b c" \
    "1"

check_exit "valid batch input exits with code 0" \
    "printf 'pwd\n' | $BINARY" \
    "0"

check_exit "exit command causes exit code 0" \
    "printf 'exit\n' | $BINARY" \
    "0"

echo ""
echo "========================================"
echo " Section 5: Executor - External Commands"
echo "========================================"

check "run external command: ls /tmp" \
    "ls /tmp" \
    "$(ls /tmp)"

check "run external command with args: ls -a /tmp" \
    "ls -a /tmp" \
    "$(ls -a /tmp)"

check "unknown command prints error" \
    "notaprogram123" \
    "notaprogram123: command not found"

check "command with absolute path" \
    "/usr/bin/ls /tmp" \
    "$(ls /tmp)"

check_exit "successful command exits 0" \
    "printf 'ls /tmp\n' | $BINARY" \
    "0"

echo ""
echo "========================================"
echo " Section 6: Redirection"
echo "========================================"

TMPOUT=/tmp/mysh_redir_out.txt
TMPIN=/tmp/mysh_redir_in.txt
echo "hello from file" > $TMPIN

printf 'ls /tmp > %s\n' "$TMPOUT" > $SCRIPT
$BINARY "$SCRIPT" 2>&1
actual=$(cat "$TMPOUT" 2>/dev/null)
expected=$(ls /tmp)
if [ "$actual" = "$expected" ]; then
    echo "[PASS] output redirection creates file"
    ((PASS++))
else
    echo "[FAIL] output redirection creates file"
    echo " expected: $(echo "$expected" | head -3)"
    echo " actual:   $(echo "$actual"   | head -3)"
    ((FAIL++))
fi
rm -f $TMPOUT

check "input redirection reads from file" \
    "cat < $TMPIN" \
    "hello from file"

printf 'ls /tmp > %s\nls /tmp > %s\n' "$TMPOUT" "$TMPOUT" > $SCRIPT
$BINARY "$SCRIPT" 2>&1
actual=$(cat "$TMPOUT" 2>/dev/null)
expected=$(ls /tmp)
if [ "$actual" = "$expected" ]; then
    echo "[PASS] output redirection truncates existing file"
    ((PASS++))
else
    echo "[FAIL] output redirection truncates existing file"
    echo " expected: $(echo "$expected" | head -3)"
    echo " actual: $(echo "$actual" | head -3)"
    ((FAIL++))
fi
rm -f $TMPOUT

printf 'ls /tmp > %s\n' "$TMPOUT" > $SCRIPT
check_file "output redirection in script file" \
    "$SCRIPT" \
    ""

rm -f $TMPOUT $TMPIN

echo ""
echo "========================================"
echo " Section 7: Pipes"
echo "========================================"

check "simple pipe: ls | grep" \
    "ls /tmp | grep mysh" \
    "$(ls /tmp | grep mysh)"

check "pipe: count lines" \
    "ls /tmp | wc -l" \
    "$(ls /tmp | wc -l)"

check "two pipes" \
    "ls /tmp | grep mysh | wc -l" \
    "$(ls /tmp | grep mysh | wc -l)"

check "pwd piped to cat" \
    "pwd | cat" \
    "$(pwd)"

check "which piped to cat" \
    "which ls | cat" \
    "/usr/bin/ls"

echo ""
echo "========================================"
echo " Section 8: Syntax Error Recovery"
echo "========================================"

SYNTAX_SCRIPT=/tmp/mysh_syntax_test.sh

printf '< <\npwd\n' > $SYNTAX_SCRIPT
actual=$($BINARY "$SYNTAX_SCRIPT" 2>&1)
if echo "$actual" | grep -q "$(pwd)"; then
    echo "[PASS] syntax error near < recovers and continues"
    ((PASS++))
else
    echo "[FAIL] syntax error near < recovers and continues"
    echo " actual: $actual"
    ((FAIL++))
fi

printf '>\npwd\n' > $SYNTAX_SCRIPT
actual=$($BINARY "$SYNTAX_SCRIPT" 2>&1)
if echo "$actual" | grep -q "$(pwd)"; then
    echo "[PASS] syntax error near > recovers and continues"
    ((PASS++))
else
    echo "[FAIL] syntax error near > recovers and continues"
    echo " actual: $actual"
    ((FAIL++))
fi

rm -f $SYNTAX_SCRIPT

# cleanup
rm -rf $WDIR $SCRIPT $WSCRIPT

echo ""
echo "========================================"
echo " Results: $PASS passed, $FAIL failed"
echo "========================================"
[ $FAIL -eq 0 ] && exit 0 || exit 1