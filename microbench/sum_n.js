// try sum 1 to 100 over 1000 iterations

function sumN(n) {
    var x = 0;

    while (n > 0) {
        x = x + n;
        n = n - 1;
    }

    return x;
}

var i = 0;

while (i < 1000) {
    void sumN(100);
    i = i + 1;
}
