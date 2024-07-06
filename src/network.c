#ifdef TARGET_LINUX
#include "network_linux.c"
#else
#error "no implementation for network.h; target OS is not supported"
#endif

bool parse_ipv4_segment(uint8_t *out, char *address, int start, int endExcl) {
    int segment_length = endExcl - start;
    if ((segment_length < 1) || (segment_length > 3)) {
        return false;
    }

    int segment = atoi(&(address[start]));

    if ((segment < 0) || (segment > 255)) {
        return false;
    }

    *out = (uint8_t) segment;

    return true;
}

bool parse_ipv4_address(uint8_t segments[4], char *address) {
    uint8_t out[4] = {0,};

    if (!address) {
        return false;
    }

    int segmentStart = 0;
    int segmentsComplete = 0;
    int i=0;
    bool complete = false;
    while (segmentsComplete < 4) {
        char ch = address[i];
        if (!ch || (ch == '.')) {
            // we can prematurely increment segmentsComplete because we discard it anyway if parsing fails
            if (!parse_ipv4_segment(&out[segmentsComplete++], address, segmentStart, i)) {
                return false;
            }

            segmentStart = i + 1;

            if (!ch) {
                complete = true;
                break;
            }
        } else if ((ch < '0') || (ch > '9')) {
            // not a digit
            return false;
        }

        i++;
    }

    if (!complete || (segmentsComplete != 4)) {
        return false;
    }

    for (int j=0; j<4; j++) {
        segments[j] = out[j];
    }

    return true;
}

bool is_ipv4_address(char *address) {
    uint8_t segments[4] = {0,};
    return parse_ipv4_address(segments, address);
}

bool parse_ipv6_segment(uint16_t *out, char *s, int length) {
    if ((length < 1) || (length > 4)) {
        return false;
    }

    uint16_t segment = 0;
    for (int i=0; i<length; i++) {
        char ch = s[i];
        if (ch >= '0' && ch <= '9') {
            segment = (segment << 4) + (ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            segment = (segment << 4) + (ch - 'a');
        } else if (ch >= 'A' && ch <= 'F') {
            segment = (segment << 4) + (ch - 'A');
        } else {
            return false;
        }
    }

    *out = segment;

    return true;
}

bool parse_ipv6_address(uint16_t segments[8], char *s) {
    uint16_t front_segments[8] = {0,};
    int num_front_segments = 0;
    uint16_t rear_segments[8] = {0,};
    int num_rear_segments = 0;
    bool expand = false;
    int i = 0;
    int segment_start = 0;

    if (!s || !segments) {
        return false;
    }

    // Having the expansion placeholder at the beginning or even standalone is hard to handle
    // without creating lots of other special cases in the parser, so it's better to treat it
    // separately.
    if (!strncmp(s, "::", 2)) {
        if (strlen(s) == 2) {
            // we have only the placeholder, nothing else
            num_front_segments = 8; // array is fully zeroed, just copy that and we are done
            goto finalize;
        } else {
            // string only starts with the placeholder but continues after that
            expand = true;
            i = 2;
            segment_start = 2;
        }
    }

    bool complete = false;
    while (num_front_segments < 8 && num_rear_segments < 8) {
        char ch = s[i++];
        char lookahead1 = ch ? s[i] : 0; // i has already been incremented for next iteration
        char lookahead2 = lookahead1 ? s[i+1] : 0;

        if (((ch >= '0') && (ch <= '9')) || ((ch >= 'a') && (ch <= 'f')) || ((ch >= 'A') && (ch <= 'F'))) {
            // valid character, will be processed later
            continue;
        } else if (!ch || (ch == ':')) {
            // premature num_..._segments increment is okay because with an invalid segment/parsing error we will never
            // use that wrongly incremented counter anyway
            uint16_t *dest = !expand ? &front_segments[num_front_segments++] : &rear_segments[num_rear_segments++];
            int length = i - segment_start - 1; // i has already been incremented, so we need to subtract one more
            if (!parse_ipv6_segment(dest, &s[segment_start], length)) {
                // segment was empty or had some other syntax error => abort
                return false;
            }

            segment_start = i; // i has already been incremented

            if (lookahead1 == ':') {
                // expansion placeholder
                if (expand) {
                    // the placeholder was already used previously => syntax error, abort
                    return false;
                }
                expand = true;

                // skip one character
                i++;
                segment_start++;

                if (!lookahead2) {
                    // end of string - quit early to avoid having to handle zero length special cases on next iteration
                    complete = true;
                    break;
                }
            } else if (!lookahead1 && (ch != ':')) {
                // end of string and address does not end in colon
                // if the address would end in a colon, continue to iterate and fail with zero segment next
                complete = true;
                break;
            }
        }
    }

    if (!complete) {
        return false;
    }

finalize:
    if (num_front_segments + num_rear_segments + (expand ? 1 : 0) > 8) {
        return false;
    }
    int num_fill = 8 - num_front_segments - num_rear_segments;
    for (int j=0; j<num_front_segments; j++) {
        segments[j] = front_segments[j];
    }
    for (int j=0; j<num_fill; j++) {
        segments[num_front_segments + j] = 0;
    }
    for (int j=0; j<num_rear_segments; j++) {
        segments[num_front_segments + num_fill + j] = rear_segments[j];
    }
    return true;
}

bool is_ipv6_address(char *address) {
    uint16_t segments[8] = {0,};
    return parse_ipv6_address(segments, address);
}

typedef int32_t _seg2i32_f(void *segments, int segment_index);

int _cmp_address(char *a, char *b, void *segments_a, void *segments_b, _seg2i32_f read_segment, bool parsed_a, bool parsed_b, int num_segments) {
    if (parsed_a && parsed_b) {
        for (int i=0; i<num_segments; i++) {
            int32_t res = read_segment(segments_a, i) - read_segment(segments_b, i);
            if (res != 0) {
                return res;
            }
        }
        return 0;
    } else if (!parsed_a) {
        // a is not an address
        if (!parsed_b) {
            // neither is an address
            if (!a) {
                // a is NULL, so we cannot call strcmp - NULL should go first
                return b ? -1 : 0;
            } else if (!b) {
                // b is NULL, a is not, so a is greater
                return 1;
            }

            return strcmp(a, b);
        }

        return -1;
    } else {
        // a is an address, b is not
        return 1;
    }
}

int32_t _read_ipv4_segment_int32(void *segments, int i) {
    return (int32_t) ((uint8_t*) segments)[i];
}

int cmp_ipv4_address(char *a, char *b) {
    uint8_t segments_a[4] = {0,};
    uint8_t segments_b[4] = {0,};

    bool parsed_a = parse_ipv4_address(segments_a, a);
    bool parsed_b = parse_ipv4_address(segments_b, b);

    return _cmp_address(a, b, segments_a, segments_b, _read_ipv4_segment_int32, parsed_a, parsed_b, 4);
}

int32_t _read_ipv6_segment_int32(void *segments, int i) {
    return (int32_t) ((uint16_t*) segments)[i];
}

int cmp_ipv6_address(char *a, char *b) {
    uint16_t segments_a[8] = {0,};
    uint16_t segments_b[8] = {0,};

    bool parsed_a = parse_ipv6_address(segments_a, a);
    bool parsed_b = parse_ipv6_address(segments_b, b);

    return _cmp_address(a, b, segments_a, segments_b, _read_ipv6_segment_int32, parsed_a, parsed_b, 8);
}

int cmp_ip_address(char *a, char *b) {
    bool a4 = is_ipv4_address(a);
    bool b4 = is_ipv4_address(b);
    bool a6 = is_ipv6_address(a);
    bool b6 = is_ipv6_address(b);

    if (a4 && b4) {
        // both are IPv4 addresses, compare them
        return cmp_ipv4_address(a,b);
    } else if (a6 && b6) {
        // both are IPv6 addresses, compare them
        return cmp_ipv6_address(a,b);
    } else if (a4 && b6) {
        // a is IPv4, b is IPv6 => IPv4 (a) goes first
        return -1;
    } else if (b4 && a6) {
        // a is IPv6, b is IPv4 => IPv4 (b) goes first
        return 1;
    } else if ((a4 || a6) && !(b4 || b6)) {
        // a is an IP address, b is not => text (b) goes first
        return 1;
    } else if (!(a4 || a6) && (b4 || b6)) {
        // b is an IP address, a is not => text (a) goes first
        return -1;
    } else {
        // neither is an IP address, compare as text
        if (!a) {
            // a is NULL, so we cannot call strcmp - NULL should go first
            return b ? -1 : 0;
        } else if (!b) {
            // b is NULL, a is not, so a is greater
            return 1;
        }

        return strcmp(a, b);
    }
}