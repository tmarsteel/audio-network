#include <unity.h>
#include <Arduino.h>
#include <network.hpp>

void test_broadcast_ip_should_work_for_netmask_24() {
    ip4_addr_t ip;
    ip4_addr_t netmask;
    ip4addr_aton("192.168.41.154", &ip);
    ip4addr_aton("255.255.255.0", &netmask);

    ip4_addr_t broadcast = network_get_broadcast_address(&ip, &netmask);
    TEST_ASSERT_EQUAL_STRING("192.168.41.255", ip4addr_ntoa(&broadcast));
}

void test_broadcast_ip_should_work_for_netmask_16() {
    ip4_addr_t ip;
    ip4_addr_t netmask;
    ip4addr_aton("192.168.41.154", &ip);
    ip4addr_aton("255.255.0.0", &netmask);

    ip4_addr_t broadcast = network_get_broadcast_address(&ip, &netmask);
    TEST_ASSERT_EQUAL_STRING("192.168.255.255", ip4addr_ntoa(&broadcast));
}

void test_broadcast_ip_should_work_for_netmask_8() {
    ip4_addr_t ip;
    ip4_addr_t netmask;
    ip4addr_aton("192.168.41.154", &ip);
    ip4addr_aton("255.0.0.0", &netmask);

    ip4_addr_t broadcast = network_get_broadcast_address(&ip, &netmask);
    TEST_ASSERT_EQUAL_STRING("192.255.255.255", ip4addr_ntoa(&broadcast));
}

void test_broadcast_ip_should_work_for_netmask_19() {
    ip4_addr_t ip;
    ip4_addr_t netmask;
    ip4addr_aton("143.93.128.0", &ip);
    ip4addr_aton("255.255.224.0", &netmask);

    ip4_addr_t broadcast = network_get_broadcast_address(&ip, &netmask);
    TEST_ASSERT_EQUAL_STRING("143.93.159.255", ip4addr_ntoa(&broadcast));
}

#ifndef UNIT_TEST
void setup() {
    delay(4000);
    UNITY_BEGIN();
    RUN_TEST(test_broadcast_ip_should_work_for_netmask_24);
    RUN_TEST(test_broadcast_ip_should_work_for_netmask_16);
    RUN_TEST(test_broadcast_ip_should_work_for_netmask_8);
    RUN_TEST(test_broadcast_ip_should_work_for_netmask_19);
    UNITY_END();
}

void loop() {

}
#endif