#include <iostream>
#include <fstream>
#include <string>
#include <array>
#include <vector>
#include <cstdio>  //printf do operacji na plikach
#include <set>  //eliminowanie duplikatów

#include "include/json.hpp"

#ifdef _WIN32 // komunikacja sieciowa w systieme windows
    #include <winsock2.h>
    #include <ws2tcpip.h> 
#else  //operacje sieciowe i niskopoziomowe
    #include <unistd.h>  
    #include <fcntl.h>
    #include <arpa/inet.h>
    #include <sys/socket.h>
    #include <errno.h>
#endif

#ifdef _WIN32  // bez tego httplib.h nie działa (co najmniej windows8)
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0A00
    #endif
#endif

#include "include/httplib.h"  // do symulowania strony http

using json = nlohmann::json;

// ===================== STRUKTURY =====================

struct Commune {
    std::string communeName;
    std::string districtName;
    std::string provinceName;
};

struct City {
    int id = -1;
    std::string name;
    Commune commune;
};

struct Station {
    int id = -1;
    std::string stationName;
    std::string gegrLat;
    std::string gegrLon;
    City city;
    std::string addressStreet;
};

struct Sensor {
    int sensorId = -1;
    int stationId = -1;
    std::string paramName;
    std::string paramFormula;
    std::string paramCode;
    int idParam = -1;
};

// ===================== POMOCNICZE =====================  gdyby funkcja nie otrzymała wartości tekstowej, zwraca wartość domyślną

std::string getStringOrDefault(const json& obj, const std::string& key, const std::string& def) {
    if (!obj.contains(key) || obj[key].is_null()) return def;
    if (obj[key].is_string()) return obj[key].get<std::string>();
    return def;
}

// ===================== INTERNET =====================

bool isInternetAvailable(int timeoutMs = 2000) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;
#endif

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &addr.sin_addr);

#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    fcntl(sock, F_SETFL, O_NONBLOCK);
#endif

    connect(sock, (sockaddr*)&addr, sizeof(addr));

    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(sock, &fdset);

    timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int result = select(sock + 1, nullptr, &fdset, nullptr, &tv);

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    return result > 0;
}

// ===================== IO =====================

bool saveToFile(const std::string& fileName, const std::string& content) {  //zapisywanie do pliku + bool czy się powiodło
    std::ofstream file(fileName, std::ios::binary);
    if (!file.is_open()) return false;
    file << content;
    return true;
}

bool saveJsonToFile(const std::string& fileName, const json& data) {  //zapis danych json do pliku
    std::ofstream file(fileName);
    if (!file.is_open()) return false;
    file << data.dump(4);
    return true;
}

std::string executeCommand(const std::string& command) {  //uruchamia polecenie systemowe
    std::array<char, 4096> buffer{};
    std::string result;

#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif

    if (!pipe) return "";

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        result += buffer.data();
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    return result;
}

// ===================== API =====================

std::string fetchUrl(const std::string& url) {  //curl-zapytanie http, funkcja pobiera zawartość wskazanego url
#ifdef _WIN32
    std::string command = "curl.exe -s \"" + url + "\"";
#else
    std::string command = "curl -s \"" + url + "\"";
#endif

    return executeCommand(command);
}

json fetchAllStationsJson() {  // funkcja pobiera stacje pomiarowe z API GIOŚ ze wszystkich stron wyników i wkleja w jeden obiekt json
    const std::string baseUrl = "https://api.gios.gov.pl/pjp-api/v1/rest/station/findAll";

    json finalJson;
    finalJson["Lista stacji pomiarowych"] = json::array();

    std::string firstPageText = fetchUrl(baseUrl);
    if (firstPageText.empty()) {
        return finalJson;
    }

    json firstPage;
    try {
        firstPage = json::parse(firstPageText);
    } catch (...) {
        return finalJson;
    }

    int totalPages = firstPage.value("totalPages", 1);

    if (firstPage.contains("Lista stacji pomiarowych") &&
        firstPage["Lista stacji pomiarowych"].is_array()) {
        for (const auto& station : firstPage["Lista stacji pomiarowych"]) {
            finalJson["Lista stacji pomiarowych"].push_back(station);
        }
    }

    for (int page = 1; page < totalPages; page++) {
        std::string pageUrl = baseUrl + "?page=" + std::to_string(page) + "&size=20";
        std::string pageText = fetchUrl(pageUrl);

        if (pageText.empty()) {
            continue;
        }

        try {
            json pageJson = json::parse(pageText);

            if (pageJson.contains("Lista stacji pomiarowych") &&
                pageJson["Lista stacji pomiarowych"].is_array()) {
                for (const auto& station : pageJson["Lista stacji pomiarowych"]) {
                    finalJson["Lista stacji pomiarowych"].push_back(station);
                }
            }
        } catch (...) {
            continue;
        }
    }

    return finalJson;
}

json fetchSensorsForStationJson(int stationId) { // pobiera liste sensorów dla podanej stacji i zwraca obiekt json
    const std::string url =
        "https://api.gios.gov.pl/pjp-api/v1/rest/station/sensors/" + std::to_string(stationId);

    std::string responseText = fetchUrl(url);
    if (responseText.empty()) {
        return json{};
    }

    try {
        return json::parse(responseText);
    } catch (...) {
        return json{};
    }
}

json fetchMeasurementsForSensorJson(int sensorId) {  //pobieranie danych pomiarowych dla konkretnego sensora
    const std::string url =
        "https://api.gios.gov.pl/pjp-api/v1/rest/data/getData/" + std::to_string(sensorId);

    std::string responseText = fetchUrl(url);
    if (responseText.empty()) {
        return json{};
    }

    try {
        return json::parse(responseText);
    } catch (...) {
        return json{};
    }
}

const json* findMeasurementsArray(const json& rawData) {  // wyszukuje w obiekcie json tablice zawietającą dane pomiarowe i zwraca do niej wskaźniki
    if (rawData.contains("values") && rawData["values"].is_array()) {
        return &rawData["values"];
    }

    if (rawData.contains("Lista danych pomiarowych") &&
        rawData["Lista danych pomiarowych"].is_array()) {
        return &rawData["Lista danych pomiarowych"];
    }

    for (auto it = rawData.begin(); it != rawData.end(); ++it) {
        if (it.value().is_array()) {
            return &it.value();
        }
    }

    return nullptr;
}

bool loadJsonFromFile(const std::string& fileName, json& data) {  // wczytuje dane json do pliku, zwraca czy operacja się powiodła
    std::ifstream file(fileName);
    if (!file.is_open()) return false;

    try {
        file >> data;
    } catch (...) {
        return false;
    }

    return true;
}

// ===================== PARSER =====================

std::vector<Station> parseStations(const json& stationsJson) {  // przetwarza dane json ze stacjami na wektor struktur "Station"
    std::vector<Station> stations;

    if (!stationsJson.contains("Lista stacji pomiarowych")) return stations;

    const auto& array = stationsJson["Lista stacji pomiarowych"];
    if (!array.is_array()) return stations;

    for (const auto& item : array) {
        Station station{};

        station.id = item.value("Identyfikator stacji", -1);
        station.stationName = getStringOrDefault(item, "Nazwa stacji", "Brak nazwy");
        station.gegrLat = getStringOrDefault(item, "WGS84 φ N", "Brak szerokosci");
        station.gegrLon = getStringOrDefault(item, "WGS84 λ E", "Brak dlugosci");
        station.addressStreet = getStringOrDefault(item, "Ulica", "Brak ulicy");

        station.city.id = item.value("Identyfikator miasta", -1);
        station.city.name = getStringOrDefault(item, "Nazwa miasta", "Brak miasta");

        station.city.commune.communeName = getStringOrDefault(item, "Gmina", "");
        station.city.commune.districtName = getStringOrDefault(item, "Powiat", "");
        station.city.commune.provinceName = getStringOrDefault(item, "Województwo", "");

        stations.push_back(station);
    }

    return stations;
}

std::vector<Sensor> parseSensors(const json& sensorsJson) {  // przetwarza dane json z sensorami na wektor struktur "Sensor"
    std::vector<Sensor> sensors;

    const json* array = nullptr;

    if (sensorsJson.contains("Lista stanowisk pomiarowych") &&
        sensorsJson["Lista stanowisk pomiarowych"].is_array()) {
        array = &sensorsJson["Lista stanowisk pomiarowych"];
    } else {
        for (auto it = sensorsJson.begin(); it != sensorsJson.end(); ++it) {
            if (it.value().is_array()) {
                array = &it.value();
                break;
            }
        }
    }

    if (array == nullptr) {
        return sensors;
    }

    for (const auto& item : *array) {
        Sensor sensor{};

        sensor.sensorId = item.value("Identyfikator stanowiska", -1);
        sensor.stationId = item.value("Identyfikator stacji", -1);
        sensor.paramName = getStringOrDefault(item, "Wskaźnik", "Brak nazwy");
        sensor.paramFormula = getStringOrDefault(item, "Wskaźnik - wzór", "Brak symbolu");
        sensor.paramCode = getStringOrDefault(item, "Wskaźnik - kod", "Brak kodu");
        sensor.idParam = item.value("Id wskaźnika", -1);

        sensors.push_back(sensor);
    }

    return sensors;
}

json fetchMeasurementsForAllSensors(int stationId, const std::vector<Sensor>& sensors) { // pobiera dane pomiarowe dla wszystkich podanych sensorów danej stacji, zwraca jeden spójny obiekt json
    json result;
    result["stationId"] = stationId;
    result["measurements"] = json::array();

    for (const auto& sensor : sensors) {
        json rawData = fetchMeasurementsForSensorJson(sensor.sensorId);

        json entry;
        entry["stationId"] = stationId;
        entry["sensorId"] = sensor.sensorId;
        entry["paramName"] = sensor.paramName;
        entry["paramCode"] = sensor.paramCode;
        entry["paramFormula"] = sensor.paramFormula;
        entry["values"] = json::array();

        const json* valuesArray = findMeasurementsArray(rawData);

        if (valuesArray != nullptr) {
            for (const auto& item : *valuesArray) {
                if (!item.is_object()) continue;

                std::string date;

                if (item.contains("date") && item["date"].is_string()) {
                    date = item["date"].get<std::string>();
                } else if (item.contains("Data") && item["Data"].is_string()) {
                    date = item["Data"].get<std::string>();
                } else if (item.contains("Data pomiaru") && item["Data pomiaru"].is_string()) {
                    date = item["Data pomiaru"].get<std::string>();
                }

                if (date.empty()) continue;

                json value = nullptr;

                if (item.contains("value")) {
                    value = item["value"];
                } else if (item.contains("Wartość")) {
                    value = item["Wartość"];
                } else if (item.contains("Wartosc")) {
                    value = item["Wartosc"];
                }

                entry["values"].push_back({
                    {"date", date},
                    {"value", value}
                });
            }
        }

        result["measurements"].push_back(entry);
    }

    return result;
}
// aktualizacja pliku danych historyycznych bez duplikowania pomiarów o tej samej dacie.
bool updateMeasurementsHistory(const std::string& historyFileName, const json& currentMeasurements) {
    json history;

    std::ifstream input(historyFileName);

    if (input.is_open()) {
        try {
            input >> history;
        } catch (...) {
            std::cout << "Blad odczytu historii. Tworze kopie zapasowa.\n";

            std::string backupName = historyFileName + ".backup";
            std::ifstream src(historyFileName, std::ios::binary);
            std::ofstream dst(backupName, std::ios::binary);
            dst << src.rdbuf();

            history["measurements"] = json::array();
        }
    } else {
        history["measurements"] = json::array();
    }

    if (!history.contains("measurements") || !history["measurements"].is_array()) {
        std::cout << "Nieprawidlowy format historii. Nie nadpisuje pliku.\n";
        return false;
    }

    if (!currentMeasurements.contains("measurements") ||
        !currentMeasurements["measurements"].is_array()) {
        std::cout << "Nieprawidlowy format nowych pomiarow.\n";
        return false;
    }

    for (const auto& currentEntry : currentMeasurements["measurements"]) {
        int stationId = currentEntry.value("stationId", -1);
        int sensorId = currentEntry.value("sensorId", -1);

        if (stationId == -1 || sensorId == -1) {
            continue;
        }

        json* historyEntry = nullptr;

        for (auto& existingEntry : history["measurements"]) {
            if (existingEntry.value("stationId", -1) == stationId &&
                existingEntry.value("sensorId", -1) == sensorId) {
                historyEntry = &existingEntry;
                break;
            }
        }

        if (historyEntry == nullptr) {
            json newEntry;
            newEntry["stationId"] = stationId;
            newEntry["sensorId"] = sensorId;
            newEntry["paramName"] = currentEntry.value("paramName", "");
            newEntry["paramCode"] = currentEntry.value("paramCode", "");
            newEntry["paramFormula"] = currentEntry.value("paramFormula", "");
            newEntry["values"] = json::array();

            history["measurements"].push_back(newEntry);
            historyEntry = &history["measurements"].back();
        }

        if (!historyEntry->contains("values") || !(*historyEntry)["values"].is_array()) {
            (*historyEntry)["values"] = json::array();
        }

        std::set<std::string> existingDates;

        for (const auto& value : (*historyEntry)["values"]) {
            if (value.contains("date") && value["date"].is_string()) {
                existingDates.insert(value["date"].get<std::string>());
            }
        }

        if (!currentEntry.contains("values") || !currentEntry["values"].is_array()) {
            continue;
        }

        for (const auto& value : currentEntry["values"]) {
            if (!value.contains("date") || !value["date"].is_string()) {
                continue;
            }

            std::string date = value["date"].get<std::string>();

            if (existingDates.find(date) == existingDates.end()) {
                (*historyEntry)["values"].push_back(value);
                existingDates.insert(date);
            }
        }
    }

    return saveJsonToFile(historyFileName, history);
}


// ===================== WYPISYWANIE =====================

// DEBUG, wypisuje w konsoli dany sensor
void printSensors(const std::vector<Sensor>& sensors) {
    if (sensors.empty()) {
        std::cout << "Brak dostepnych sensorow dla tej stacji.\n";
        return;
    }

    std::cout << "\nDostepne sensory:\n";

    for (const auto& sensor : sensors) {
        std::cout << "----------------------------------\n";
        std::cout << "Sensor ID: " << sensor.sensorId << "\n";
        std::cout << "Station ID: " << sensor.stationId << "\n";
        std::cout << "Parametr: " << sensor.paramName << "\n";
        std::cout << "Symbol: " << sensor.paramFormula << "\n";
        std::cout << "Kod: " << sensor.paramCode << "\n";
        std::cout << "Id parametru: " << sensor.idParam << "\n";
    }
}

// uruchamia lokalny serwer HTTP, który obsuguje zapytania z aplikacji webowej
void runServer() {
    httplib::Server server;

    server.set_mount_point("/", ".");

    server.Get("/api/sensors", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("stationId")) {
            res.status = 400;
            res.set_content("{\"error\":\"Brak stationId\"}", "application/json");
            return;
        }

        int stationId = std::stoi(req.get_param_value("stationId"));

        json sensorsJson = fetchSensorsForStationJson(stationId);

        if (sensorsJson.empty()) {
            res.status = 500;
            res.set_content("{\"error\":\"Nie udalo sie pobrac sensorow\"}", "application/json");
            return;
        }

        saveJsonToFile("sensors.json", sensorsJson);

        res.set_content(sensorsJson.dump(4), "application/json");
    });

    server.Get("/api/measure", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("stationId") || !req.has_param("sensorId")) {
            res.status = 400;
            res.set_content("{\"error\":\"Brak stationId lub sensorId\"}", "application/json");
            return;
        }

        int stationId = std::stoi(req.get_param_value("stationId"));
        int sensorId = std::stoi(req.get_param_value("sensorId"));

        json sensorsJson = fetchSensorsForStationJson(stationId);
        std::vector<Sensor> sensors = parseSensors(sensorsJson);

        Sensor selectedSensor{};
        bool found = false;

        for (const auto& sensor : sensors) {
            if (sensor.sensorId == sensorId) {
                selectedSensor = sensor;
                found = true;
                break;
            }
        }

        if (!found) {
            res.status = 404;
            res.set_content("{\"error\":\"Nie znaleziono sensora\"}", "application/json");
            return;
        }

        std::vector<Sensor> selectedSensors;
        selectedSensors.push_back(selectedSensor);

        json measurementsJson = fetchMeasurementsForAllSensors(stationId, selectedSensors);

        if (!updateMeasurementsHistory("measurements_history.json", measurementsJson)) {
            res.status = 500;
            res.set_content("{\"error\":\"Nie udalo sie zapisac historii\"}", "application/json");
            return;
        }

        res.set_content(measurementsJson.dump(4), "application/json");
    });

    std::cout << "Serwer uruchomiony: http://localhost:8080\n";
    server.listen("localhost", 8080);
}

// ===================== MAIN =====================

// domyśłny tryb (brak argumentów do funkcji main) = program pobiera listę stacji
// argument server = uruchamie serwer HTTP
// argument sensor "idStacji" = pobiera sensory stacji o danym id
// argument measure "idStacji" "idSensora" pobiera pomiary danej stancji danego sensora.
// argc = liczba argumentów argv przekazanych do programu

int main(int argc, char* argv[]) {
    std::cout << "WERSJA PROGRAMU v3\n";

    if (argc >= 2 && std::string(argv[1]) == "server") { //sprawdzenie czy program uruchomiony w trybie "server"
        runServer();
        return 0;
    }


    if (argc >= 3 && std::string(argv[1]) == "sensors") {  //warunek uruchamiania pobierania sensorów dla wskazanej stacji
        int stationId = std::stoi(argv[2]);

        if (!isInternetAvailable()) {  //test z internetem
            std::cout << "Brak internetu\n";
            return 1;
        }

        json sensorsJson = fetchSensorsForStationJson(stationId);

        if (sensorsJson.empty()) {
            std::cout << "Nie udalo sie pobrac sensorow dla stacji ID: " << stationId << "\n";
            return 1;
        }

        if (!saveJsonToFile("sensors.json", sensorsJson)) {
            std::cout << "Nie udalo sie zapisac sensors.json\n";
            return 1;
        }

        std::cout << "Zapisano sensors.json dla stacji ID: " << stationId << "\n";
        return 0;
    }

    if (argc >= 4 && std::string(argv[1]) == "measure") { //warunek uruchamiania pobierania pomiarów dla sensora i nadpisywania do pliku historii
    int stationId = std::stoi(argv[2]);
    int sensorId = std::stoi(argv[3]);

    if (!isInternetAvailable()) {
        std::cout << "Brak internetu\n";
        return 1;
    }

    std::cout << "Tryb pomiaru dla stacji ID: " << stationId
              << ", sensor ID: " << sensorId << "\n";

    json sensorsJson = fetchSensorsForStationJson(stationId);
    std::vector<Sensor> sensors = parseSensors(sensorsJson);

    Sensor selectedSensor{};
    bool foundSensor = false;

    for (const auto& sensor : sensors) {
        if (sensor.sensorId == sensorId) {
            selectedSensor = sensor;
            foundSensor = true;
            break;
        }
    }

    if (!foundSensor) {
        std::cout << "Nie znaleziono sensora ID: " << sensorId
                  << " dla stacji ID: " << stationId << "\n";
        return 1;
    }

    std::vector<Sensor> selectedSensors;
    selectedSensors.push_back(selectedSensor);

    json measurementsJson = fetchMeasurementsForAllSensors(stationId, selectedSensors);

    if (!updateMeasurementsHistory("measurements_history.json", measurementsJson)) {
        std::cout << "Nie udalo sie zaktualizowac measurements_history.json\n";
        return 1;
    }

    std::cout << "Zaktualizowano measurements_history.json\n";
    return 0;
}

    // Tryb domyslny: pobranie listy stacji
    if (!isInternetAvailable()) {
        std::cout << "Brak internetu\n";
        return 1;
    }

    json data = fetchAllStationsJson();

    if (!data.contains("Lista stacji pomiarowych") ||
        !data["Lista stacji pomiarowych"].is_array() ||
        data["Lista stacji pomiarowych"].empty()) {
        std::cout << "Blad pobierania pelnej listy stacji\n";
        return 1;
    }

    if (!saveJsonToFile("stations.json", data)) {
        std::cout << "Nie udalo sie zapisac stations.json\n";
        return 1;
    }

    std::vector<Station> stations = parseStations(data);
    std::cout << "Znaleziono stacji: " << stations.size() << "\n";

    return 0;
}