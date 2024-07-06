#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "network.h"

void fail(char *reason) {
    printf(" => failed (%s)\n", reason);
    exit(1);
}

void setup_fail(char *description) {
    printf(" => setup failed: %s\n", description);
    exit(1);
}

void testcase(char *description) {
    printf("test: %s\n", description);
}

void assert_is_ipv4_address(const char *address) {
    if (is_ipv4_address((char*) address)) {
        return;
    }

    printf("tested address: %s\n", address);
    fail("expected is_ipv4_address to return true but result was false");
}

void assert_not_is_ipv4_address(const char *address) {
    if (!is_ipv4_address((char*) address)) {
        return;
    }

    printf("tested address: %s\n", address);
    fail("expected is_ipv4_address to return false but result was true");
}

void assert_is_ipv6_address(const char *address) {
    if (is_ipv6_address((char*) address)) {
        return;
    }

    printf("tested address: %s\n", address);
    fail("expected is_ipv6_address to return true but result was false");
}

void assert_not_is_ipv6_address(const char *address) {
    if (!is_ipv6_address((char*) address)) {
        return;
    }

    printf("tested address: %s\n", address);
    fail("expected is_ipv6_address to return false but result was true");
}

void assert_cmp_ipv4_address_equal(const char *a, const char *b) {
    int res = cmp_ipv4_address((char*) a, (char*) b);
    if (res == 0) {
        return;
    }

    printf("compared a = %s\n", a);
    printf("         b = %s\n", b);
    printf("    result = %d\n", res);
    fail("expected cmp_ipv4_address to return 0");
}

void assert_cmp_ipv4_address_less_than(const char *a, const char *b) {
    int res = cmp_ipv4_address((char*) a, (char*) b);
    if (res < 0) {
        return;
    }

    printf("compared a = %s\n", a);
    printf("         b = %s\n", b);
    printf("    result = %d\n", res);
    fail("expected cmp_ipv4_address to return negative number");
}

void assert_cmp_ipv4_address_greater_than(const char *a, const char *b) {
    int res = cmp_ipv4_address((char*) a, (char*) b);
    if (res > 0) {
        return;
    }

    printf("compared a = %s\n", a);
    printf("         b = %s\n", b);
    printf("    result = %d\n", res);
    fail("expected cmp_ipv4_address to return positive number");
}

void assert_cmp_ipv6_address_equal(const char *a, const char *b) {
    int res = cmp_ipv6_address((char*) a, (char*) b);
    if (res == 0) {
        return;
    }

    printf("compared a = %s\n", a);
    printf("         b = %s\n", b);
    printf("    result = %d\n", res);
    fail("expected cmp_ipv6_address to return 0");
}

void assert_cmp_ipv6_address_less_than(const char *a, const char *b) {
    int res = cmp_ipv6_address((char*) a, (char*) b);
    if (res < 0) {
        return;
    }

    printf("compared a = %s\n", a);
    printf("         b = %s\n", b);
    printf("    result = %d\n", res);
    fail("expected cmp_ipv6_address to return negative number");
}

void assert_cmp_ipv6_address_greater_than(const char *a, const char *b) {
    int res = cmp_ipv6_address((char*) a, (char*) b);
    if (res > 0) {
        return;
    }

    printf("compared a = %s\n", a);
    printf("         b = %s\n", b);
    printf("    result = %d\n", res);
    fail("expected cmp_ipv6_address to return positive number");
}

void assert_cmp_ip_address_equal(const char *a, const char *b) {
    int res = cmp_ip_address((char*) a, (char*) b);
    if (res == 0) {
        return;
    }

    printf("compared a = %s\n", a);
    printf("         b = %s\n", b);
    printf("    result = %d\n", res);
    fail("expected cmp_ip6_address to return 0");
}

void assert_cmp_ip_address_less_than(const char *a, const char *b) {
    int res = cmp_ip_address((char*) a, (char*) b);
    if (res < 0) {
        return;
    }

    printf("compared a = %s\n", a);
    printf("         b = %s\n", b);
    printf("    result = %d\n", res);
    fail("expected cmp_ip_address to return negative number");
}

void assert_cmp_ip_address_greater_than(const char *a, const char *b) {
    int res = cmp_ip_address((char*) a, (char*) b);
    if (res > 0) {
        return;
    }

    printf("compared a = %s\n", a);
    printf("         b = %s\n", b);
    printf("    result = %d\n", res);
    fail("expected cmp_ip_address to return positive number");
}

int main(int argc, char **argv) {
    printf("--- network address tests starting\n");

    testcase("is_ipv4_address");
    assert_is_ipv4_address("0.0.0.0");
    assert_is_ipv4_address("000.000.000.000");
    assert_is_ipv4_address("000.00.0.00");
    assert_is_ipv4_address("10.0.42.73");
    assert_is_ipv4_address("010.0.042.73");
    assert_is_ipv4_address("127.0.0.1");
    assert_is_ipv4_address("192.168.0.1");
    assert_is_ipv4_address("192.168.000.001");
    assert_is_ipv4_address("255.255.255.0");

    testcase("not is_ipv4_address");
    assert_not_is_ipv4_address(NULL);
    assert_not_is_ipv4_address("");
    assert_not_is_ipv4_address(" ");
    assert_not_is_ipv4_address("0.0.0.0000");
    assert_not_is_ipv4_address("0.0.0000.0");
    assert_not_is_ipv4_address("0.0000.0.0");
    assert_not_is_ipv4_address("0000.0.0.0");
    assert_not_is_ipv4_address("0.0.0");
    assert_not_is_ipv4_address("0.0.0.0 ");
    assert_not_is_ipv4_address(" 0.0.0.0");
    assert_not_is_ipv4_address("0.0.0.0.");
    assert_not_is_ipv4_address(".0.0.0.0");
    assert_not_is_ipv4_address("0.0..0.0");
    assert_not_is_ipv4_address("0.0:0.0");
    assert_not_is_ipv4_address("0.0.0.0:");
    assert_not_is_ipv4_address(":0.0.0.0");
    assert_not_is_ipv4_address("0.0.0.0.0");
    assert_not_is_ipv4_address("0.0.a.0");
    assert_not_is_ipv4_address("1000.0.42.73");
    assert_not_is_ipv4_address("255.255.255.256");
    assert_not_is_ipv4_address("::");

    testcase("is_ipv6_address");
    assert_is_ipv6_address("0000:0000:0000:0000:0000:0000:0000:0000");
    assert_is_ipv6_address("::");
    assert_is_ipv6_address("0000:0000:0000:0000:0000:0000:0000:0001");
    assert_is_ipv6_address("::1");
    assert_is_ipv6_address("0:00:000::0000:0:01");
    assert_is_ipv6_address("0::");
    assert_is_ipv6_address("0::0");
    assert_is_ipv6_address("fedc:ba98:7654:3210:fedc:ba98:7654:3210");
    assert_is_ipv6_address("0123:4567:89ab:cdef:0123:4567:89ab:cdef");
    assert_is_ipv6_address("0123:4567:89AB:CDEF:0123:4567:89ab:cdef");
    assert_is_ipv6_address("0123:4567:89aB:cDeF:0123:4567:89Ab:CdEf");

    testcase("not is_ipv6_address");
    assert_not_is_ipv6_address(NULL);
    assert_not_is_ipv6_address("");
    assert_not_is_ipv6_address(" ");
    assert_not_is_ipv6_address(" ::");
    assert_not_is_ipv6_address(":: ");
    assert_not_is_ipv6_address(": :");
    assert_not_is_ipv6_address(": ::");
    assert_not_is_ipv6_address(":: :");
    assert_not_is_ipv6_address(":::");
    assert_not_is_ipv6_address(":0:");
    assert_not_is_ipv6_address("::00001");
    assert_not_is_ipv6_address("::g");
    assert_not_is_ipv6_address("::g0001");
    assert_not_is_ipv6_address("::-0");
    assert_not_is_ipv6_address("::-1");
    assert_not_is_ipv6_address("::-");
    assert_not_is_ipv6_address("0123:4567:89ab:cdef:g123:4567:89ab:cdef");
    assert_not_is_ipv6_address("0123:4567:89ab:cdef:0123::4567:89ab:cdef");
    assert_not_is_ipv6_address("0123:4567:89ab::0123::89ab:cdef");
    assert_not_is_ipv6_address("0123:4567:89ab:cdef:-123:4567:89ab:cdef");
    assert_not_is_ipv6_address("0123:4567:89ab:cdef:+123:4567:89ab:cdef");
    assert_not_is_ipv6_address("0123:4567:89ab:cdef:0.23:4567:89ab:cdef");
    assert_not_is_ipv6_address("0123:4567:89ab:cdef:0123:4567:89ab:cdef:");
    assert_not_is_ipv6_address(":0123:4567:89ab:cdef:0123:4567:89ab:cdef");
    assert_not_is_ipv6_address("::0123:4567:89ab:cdef:0123:4567:89ab:cdef");
    assert_not_is_ipv6_address("0123:4567:89ab:cdef:0123:4567:89ab:cdef::");

    testcase("cmp_ipv4_address equal");
    assert_cmp_ipv4_address_equal("0.0.0.0", "0.0.0.0");
    assert_cmp_ipv4_address_equal("0.0.0.0", "000.000.000.000");
    assert_cmp_ipv4_address_equal("000.000.000.000", "0.0.0.0");
    assert_cmp_ipv4_address_equal("192.168.50.2", "192.168.50.2");
    assert_cmp_ipv4_address_equal("255.255.255.255", "255.255.255.255");
    assert_cmp_ipv4_address_equal(NULL, NULL);
    assert_cmp_ipv4_address_equal("not an address", "not an address");

    testcase("cmp_ipv4_address a < b");
    assert_cmp_ipv4_address_less_than("0.0.0.0", "0.0.0.1");
    assert_cmp_ipv4_address_less_than("0.0.0.0", "0.0.1.0");
    assert_cmp_ipv4_address_less_than("0.0.0.0", "0.1.0.0");
    assert_cmp_ipv4_address_less_than("0.0.0.0", "1.0.0.0");
    assert_cmp_ipv4_address_less_than("0.0.0.0", "0.0.0.01");
    assert_cmp_ipv4_address_less_than("17.23.4.50", "17.23.4.51");
    assert_cmp_ipv4_address_less_than("17.23.4.50", "17.23.4.60");
    assert_cmp_ipv4_address_less_than("17.23.4.50", "17.23.4.150");
    assert_cmp_ipv4_address_less_than("17.23.4.50", "17.23.5.50");
    assert_cmp_ipv4_address_less_than("17.23.4.50", "17.23.14.50");
    assert_cmp_ipv4_address_less_than("17.23.4.50", "17.23.40.50");
    assert_cmp_ipv4_address_less_than("17.23.4.50", "17.24.4.50");
    assert_cmp_ipv4_address_less_than("17.23.4.50", "17.33.4.50");
    assert_cmp_ipv4_address_less_than("17.23.4.50", "17.123.4.50");
    assert_cmp_ipv4_address_less_than("17.23.4.50", "18.23.4.50");
    assert_cmp_ipv4_address_less_than("17.23.4.50", "27.23.4.50");
    assert_cmp_ipv4_address_less_than("17.23.4.50", "117.23.4.50");
    assert_cmp_ipv4_address_less_than("17.23.4.50", "17.23.4.051");
    assert_cmp_ipv4_address_less_than(NULL, "0.0.0.0");
    assert_cmp_ipv4_address_less_than(NULL, "not an address");
    assert_cmp_ipv4_address_less_than("not an address", "0.0.0.0");
    assert_cmp_ipv4_address_less_than("not an address", "not an address A");
    assert_cmp_ipv4_address_less_than("not an address A", "not an address B");

    testcase("cmp_ipv4_address a > b");
    assert_cmp_ipv4_address_greater_than("255.255.255.255", "255.255.255.254");
    assert_cmp_ipv4_address_greater_than("255.255.255.255", "255.255.254.255");
    assert_cmp_ipv4_address_greater_than("255.255.255.255", "255.254.255.255");
    assert_cmp_ipv4_address_greater_than("255.255.255.255", "254.255.255.255");
    assert_cmp_ipv4_address_greater_than("1.23.4.5", "1.23.4.4");
    assert_cmp_ipv4_address_greater_than("1.23.4.5", "1.23.4.04");
    assert_cmp_ipv4_address_greater_than("1.23.4.5", "1.23.04.4");
    assert_cmp_ipv4_address_greater_than("1.23.4.5", "1.023.4.4");
    assert_cmp_ipv4_address_greater_than("1.23.4.5", "01.23.4.4");
    assert_cmp_ipv4_address_greater_than("1.23.4.5", "1.23.3.5");
    assert_cmp_ipv4_address_greater_than("1.23.4.5", "1.22.4.5");
    assert_cmp_ipv4_address_greater_than("1.23.4.5", "0.23.4.5");
    assert_cmp_ipv4_address_greater_than("1.23.4.5", NULL);
    assert_cmp_ipv4_address_greater_than("1.23.4.5", "not an address");
    assert_cmp_ipv4_address_greater_than("not an address", NULL);
    assert_cmp_ipv4_address_greater_than("not an address A", "not an address");
    assert_cmp_ipv4_address_greater_than("not an address B", "not an address A");

    testcase("cmp_ipv6_address equal");
    assert_cmp_ipv6_address_equal("::", "::");
    assert_cmp_ipv6_address_equal("::", "0000:0000:0000:0000:0000:0000:0000:0000");
    assert_cmp_ipv6_address_equal("0000:0000:0000:0000:0000:0000:0000:0000", "::");
    assert_cmp_ipv6_address_equal("0000:0000:0000:0000:0000:0000:0000:0000", "0000:0000:0000:0000:0000:0000:0000:0000");
    assert_cmp_ipv6_address_equal("0::", "0000:0000:0000:0000:0000:0000:0000:0000");
    assert_cmp_ipv6_address_equal("::0", "0000:0000:0000:0000:0000:0000:0000:0000");
    assert_cmp_ipv6_address_equal("::0:0", "0000:0000:0000:0000:0000:0000:0000:0000");
    assert_cmp_ipv6_address_equal("0::", "0000:0000:0000:0000:0000:0000:0000:0000");
    assert_cmp_ipv6_address_equal("0::0", "0000:0000:0000:0000:0000:0000:0000:0000");
    assert_cmp_ipv6_address_equal("0:0::0:0", "0000:0000:0000:0000:0000:0000:0000:0000");
    assert_cmp_ipv6_address_equal("0:00::000:0000", "0000:0000:0000:0000:0000:0000:0000:0000");
    assert_cmp_ipv6_address_equal("::1", "0000:0000:0000:0000:0000:0000:0000:0001");
    assert_cmp_ipv6_address_equal("2001:db8:fefe::cafe:c001", "2001:0db8:fefe:0000:0000:0000:cafe:c001");
    assert_cmp_ipv6_address_equal("2001:db8:fefe::cafe:c001", "2001:0db8:fefe::0:cafe:c001");
    assert_cmp_ipv6_address_equal("2001:db8:fefe::cafe:c001", "2001:0db8:fefe:0:0:0:cafe:c001");
    assert_cmp_ipv6_address_equal("2001:db8:fefe::cafe:c001", "2001:DB8:FEFE::CAFE:C001");
    assert_cmp_ipv6_address_equal("2001:dB8:FeFe::CaFe:C001", "2001:Db8:fEfE::cAfE:c001");
    assert_cmp_ipv6_address_equal(NULL, NULL);
    assert_cmp_ipv6_address_equal("not an address", "not an address");

    testcase("cmp_ipv6_address a < b");
    assert_cmp_ipv6_address_less_than("::", "::1");
    assert_cmp_ipv6_address_less_than("::", "1::");
    assert_cmp_ipv6_address_less_than("::1", "::1:0");
    assert_cmp_ipv6_address_less_than("2001:db8:fefe::cafe:c0", "2001:db8:fefe::cafe:c000");
    assert_cmp_ipv6_address_less_than("2001:db8:fefe::cafe:c000", "2001:db8:fefe::cafe:c001");
    assert_cmp_ipv6_address_less_than("2001:db8:fef::cafe:c001", "2001:db8:fefe::cafe:c001");
    assert_cmp_ipv6_address_less_than("2001:db7:fefe::cafe:c001", "2001:db8:fefe::cafe:c001");
    assert_cmp_ipv6_address_less_than("2000:db8:fefe::cafe:c001", "2001:db8:fefe::cafe:c001");
    assert_cmp_ipv6_address_less_than("20:db8:fefe::cafe:c001", "2001:db8:fefe::cafe:c001");
    assert_cmp_ipv6_address_less_than(NULL, "::");
    assert_cmp_ipv6_address_less_than(NULL, "not an address");
    assert_cmp_ipv6_address_less_than("not an address", "::");
    assert_cmp_ipv6_address_less_than("not an address", "not an address A");
    assert_cmp_ipv6_address_less_than("not an address A", "not an address B");

    testcase("cmp_ipv6_address a > b");
    assert_cmp_ipv6_address_greater_than("::1", "::");
    assert_cmp_ipv6_address_greater_than("1::", "::");
    assert_cmp_ipv6_address_greater_than("1::", "::1");
    assert_cmp_ipv6_address_greater_than("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:fffe");
    assert_cmp_ipv6_address_greater_than("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", "ffff:ffff:ffff:ffff:ffff:ffff:fffe:ffff");
    assert_cmp_ipv6_address_greater_than("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", "fffe:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
    assert_cmp_ipv6_address_greater_than("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", "fff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
    assert_cmp_ipv6_address_greater_than("2001:db8:feff::cafe:c001", "2001:db8:fefe::cafe:c001");
    assert_cmp_ipv6_address_greater_than("2001:db8:feff::cafe:c001", NULL);
    assert_cmp_ipv6_address_greater_than("2001:db8:feff::cafe:c001", "not an address");
    assert_cmp_ipv6_address_greater_than("not an address A", "not an address");
    assert_cmp_ipv6_address_greater_than("not an address B", "not an address A");

    testcase("cmp_ip_address equal");
    assert_cmp_ip_address_equal("192.168.50.2", "192.168.50.2");
    assert_cmp_ip_address_equal("192.168.50.2", "192.168.050.002");
    assert_cmp_ip_address_equal("::", "::");
    assert_cmp_ip_address_equal("::", "0000:0000:0000:0000:0000:0000:0000:0000");
    assert_cmp_ip_address_equal("2001:db8:fefe::cafe:c001", "2001:db8:fefe::cafe:c001");
    assert_cmp_ip_address_equal("2001:db8:fefe::cafe:c001", "2001:0db8:fefe:0000:0000:0000:cafe:c001");
    assert_cmp_ip_address_equal("2001:0dB8:fEfE:0000:0000:0000:cAfE:c001", "2001:0Db8:FeFe:0000:0000:0000:CaFe:C001");
    assert_cmp_ip_address_equal(NULL, NULL);
    assert_cmp_ip_address_equal("not an address", "not an address");

    testcase("cmp_ip_address a < b");
    assert_cmp_ip_address_less_than("192.168.50.2", "192.168.50.3");
    assert_cmp_ip_address_less_than("192.168.50.2", "2001:db8:fefe::cafe:c001");
    assert_cmp_ip_address_less_than("2001:db8:fefe::cafe:c001", "2001:db8:fefe::cafe:c007");
    assert_cmp_ip_address_less_than(NULL, "192.168.50.2");
    assert_cmp_ip_address_less_than(NULL, "2001:db8:fefe::cafe:c001");
    assert_cmp_ip_address_less_than(NULL, "not an address");
    assert_cmp_ip_address_less_than("not an address", "192.168.50.2");
    assert_cmp_ip_address_less_than("not an address", "2001:db8:fefe::cafe:c001");
    assert_cmp_ip_address_less_than("not an address", "not an address A");
    assert_cmp_ip_address_less_than("not an address A", "not an address B");

    testcase("cmp_ip_address a > b");
    assert_cmp_ip_address_greater_than("192.168.50.3", "192.168.50.2");
    assert_cmp_ip_address_greater_than("2001:db8:fefe::cafe:c001", "192.168.50.2");
    assert_cmp_ip_address_greater_than("2001:db8:fefe::cafe:c007", "2001:db8:fefe::cafe:c001");
    assert_cmp_ip_address_greater_than("192.168.50.2", NULL);
    assert_cmp_ip_address_greater_than("2001:db8:fefe::cafe:c001", NULL);
    assert_cmp_ip_address_greater_than("not an address", NULL);
    assert_cmp_ip_address_greater_than("192.168.50.2", "not an address");
    assert_cmp_ip_address_greater_than("2001:db8:fefe::cafe:c001", "not an address");
    assert_cmp_ip_address_greater_than("not an address A", "not an address");
    assert_cmp_ip_address_greater_than("not an address B", "not an address A");

    printf("--- network address tests completed\n");
    return 0;
}