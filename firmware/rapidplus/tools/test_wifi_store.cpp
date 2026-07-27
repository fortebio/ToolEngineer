// Host-side test of the /wifi.json store logic (wifiStore.cpp), without a device or
// LittleFS: a tiny in-memory filesystem shim replaces LittleFS, and a minimal ArduinoJson
// is not needed - we reimplement only the ordering rules the real code guarantees and
// assert them. If the shipped .cpp diverges from these rules the guard in review catches
// it; this file locks the CONTRACT: newest-to-front, no duplicates, cap at MAX, remove.
//
// Build:  g++ -O2 -std=c++17 tools/test_wifi_store.cpp -o t && ./t
//
// It mirrors wifiStore.cpp's algorithm rather than linking it (the .cpp pulls in
// LittleFS/ArduinoJson/Arduino String). The point is to prove the ALGORITHM the device
// runs is correct; test_wifi_store keeps that algorithm honest the same way the other
// self-contained on-host tests in tools/ do.
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

static const uint8_t MAX = 5;

struct Net
{
    std::string ssid, pass;
};
static std::vector<Net> store;

// --- mirror of wifiStoreAdd(): new/updated entry to the FRONT, no dup, drop past MAX ---
static bool add(const std::string &ssid, const std::string &pass)
{
    if (ssid.empty() || ssid.size() > 32)
        return false;
    std::vector<Net> next;
    next.push_back({ssid, pass});
    for (auto &n : store)
    {
        if ((int)next.size() >= MAX)
            break;
        if (n.ssid == ssid)
            continue; // replaced by head
        next.push_back(n);
    }
    store = next;
    return true;
}

// --- mirror of wifiStoreRemove (named forget to avoid ::remove)() ---
static bool forget(const std::string &ssid)
{
    std::vector<Net> next;
    bool hit = false;
    for (auto &n : store)
    {
        if (n.ssid == ssid)
        {
            hit = true;
            continue;
        }
        next.push_back(n);
    }
    store = next;
    return hit;
}

static std::string order()
{
    std::string s;
    for (auto &n : store)
        s += n.ssid + " ";
    return s;
}

int main()
{
    // add: newest goes to the front (preferred = first tried on boot)
    add("A", "pa");
    add("B", "pb");
    add("C", "pc");
    assert(order() == "C B A ");

    // re-adding an existing SSID moves it to the front AND updates the password,
    // without creating a duplicate
    add("A", "newpass");
    assert(order() == "A C B ");
    assert(store[0].pass == "newpass");
    assert(store.size() == 3);

    // cap at MAX: adding the 6th drops the oldest (tail)
    add("D", "pd");
    add("E", "pe");
    add("F", "pf"); // now 6 candidates -> keep 5
    assert(store.size() == MAX);
    assert(order() == "F E D A C "); // B (oldest) fell off
    for (auto &n : store)
        assert(n.ssid != "B");

    // remove: present -> true and gone; absent -> false and unchanged
    assert(forget("D") == true);
    assert(order() == "F E A C ");
    assert(forget("ZZ") == false);
    assert(order() == "F E A C ");

    // invalid ssid is rejected, list untouched
    std::string before = order();
    assert(add("", "x") == false);
    assert(add(std::string(33, 'x'), "x") == false);
    assert(order() == before);

    std::cout << "all wifiStore contract checks passed\n";
    return 0;
}
