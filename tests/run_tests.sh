#!/usr/bin/env bash
# Feeds commands to the shell on stdin and compares stdout+stderr with the
# expected text. Usage: tests/run_tests.sh [path-to-shell]   (default ./shell)

BIN="${1:-./shell}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
pass=0
fail=0

report() { # report <name> <ok:0/1> <expected> <actual>
    if [ "$2" -eq 0 ]; then
        pass=$((pass + 1)); echo "ok   - $1"
    else
        fail=$((fail + 1)); echo "FAIL - $1"
        echo "  expected: $3"; echo "  actual:   $4"
    fi
}

check() { # check <name> <commands> <expected output>
    local actual
    actual="$(printf '%s\n' "$2" | "$BIN" 2>&1)"
    [ "$actual" == "$3" ]; report "$1" $? "$3" "$actual"
}

check_contains() { # check_contains <name> <commands> <substring>
    local actual
    actual="$(printf '%s\n' "$2" | "$BIN" 2>&1)"
    [[ "$actual" == *"$3"* ]]; report "$1" $? "...$3..." "$actual"
}

check_exit() { # check_exit <name> <commands> <expected exit code>
    printf '%s\n' "$2" | "$BIN" >/dev/null 2>&1
    local code=$?
    [ "$code" -eq "$3" ]; report "$1" $? "exit $3" "exit $code"
}

check "simple command"        "echo hello"                          "hello"
check "blank lines ignored"   $'\n   \necho ok'                     "ok"
check "single pipe"           "echo hello | tr a-z A-Z"             "HELLO"
check "three-stage pipeline"  "printf 'b\\na\\nb\\n' | sort | uniq" $'a\nb'
check "pipe status is last"   $'false | true\necho $?'              "0"

check "output redirect"       "echo hi > $TMP/o.txt
cat < $TMP/o.txt"                                                    "hi"
check "append redirect"       "echo one > $TMP/a.txt
echo two >> $TMP/a.txt
cat $TMP/a.txt"                                                      $'one\ntwo'
check "pipe + redirect"       "echo hello | tr a-z A-Z > $TMP/p.txt
cat $TMP/p.txt"                                                      "HELLO"
check "input + output redirect" "printf 'b\\na\\n' > $TMP/in.txt
sort < $TMP/in.txt > $TMP/out.txt
cat $TMP/out.txt"                                                    $'a\nb'

check "double and single quotes" "echo \"a   b\" 'c  d'"            "a   b c  d"
check "backslash escape"      "echo a\\ b"                          "a b"
check "quoted pipe is literal" "echo 'a|b'"                         "a|b"
check "export and \$VAR"      $'export FOO=bar\necho $FOO "$FOO" \'$FOO\''  'bar bar $FOO'
check "tilde expansion"       $'export HOME=/x/y\necho ~ ~/z'       "/x/y /x/y/z"
check "\$? after failure"     $'false\necho $?'                     "1"

check "command not found"     $'nosuchcmd_xyz\necho $?'             $'shell: nosuchcmd_xyz: No such file or directory\n127'
check "cd and pwd"            $'cd /\npwd'                          "/"
check "cd failure"            $'cd /no/such/dir\necho $?'           $'cd failed: No such file or directory\n1'
check "cd -"                  $'cd /\ncd /usr\ncd -\npwd'           $'/\n/'

check "syntax error recovers" $'echo hi |\necho still alive'        $'shell: syntax error: missing command\nstill alive'
check "unterminated quote"    $'echo "oops\necho next'              $'shell: unterminated quote\nnext'
check_contains "too many args rejected" "echo $(seq -s " " 1 100)" "too many arguments"
check "background returns"    $'sleep 0.2 &\necho after'            "after"
check_exit "exit code"        "exit 3"                              3
check_exit "exit uses \$?"    $'false\nexit'                        1

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
