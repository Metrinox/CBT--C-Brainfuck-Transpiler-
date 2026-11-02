char a[6];
a[0] = 53; a[1] = 50; a[2] = 57; a[3] = 49; a[4] = 55; a[5] = 51;
for (char i = 0; i < 5; i = i + 1) {
    char lim = 5 - i;
    for (char j = 0; j < lim; j = j + 1) {
        char p = a[j];
        char q = a[j + 1];
        if (p > q) {
            a[j] = q;
            a[j + 1] = p;
        }
    }
}
for (char k = 0; k < 6; k = k + 1) {
    putchar(a[k]);
}
