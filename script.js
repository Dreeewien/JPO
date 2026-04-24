let stations = []; // deklaracja zmiennej przechowującą liste stacji

const appState = {  // przechowuje aktualnie wybrane opcje
    selectedStation: null,
    selectedSensor: null,
    selectedRange: null,
    sensors: [],
    measurements: [],
    autoMeasurementEnabled: false
};

// elementy HTML
const provinceSelect = document.getElementById("province");
const districtSelect = document.getElementById("district");
const communeSelect = document.getElementById("commune");
const stationSelect = document.getElementById("station");
const output = document.getElementById("output");
const measurementsList = document.getElementById("measurements-list");
const sensorSelect = document.getElementById("sensorSelect");
const rangeSelect = document.getElementById("rangeSelect");
const minValue = document.getElementById("minValue");
const maxValue = document.getElementById("maxValue");
const chartCanvas = document.getElementById("measurementChart");
let measurementChart = null;


// pobiera dane stations.json, zapisuje do zmiennej stations, inicjalizuje listę województw
fetch("stations.json")
    .then(response => {
        if (!response.ok) throw new Error("Nie udalo sie wczytac stations.json");
        return response.json();
    })
    .then(data => {
        stations = data["Lista stacji pomiarowych"] || [];
        fillProvinces();
    })
    .catch(error => {
        console.error("Blad:", error);
        output.textContent = "Blad wczytywania danych";
    });

function uniqueSorted(values) {  // usuwa duplikaty i puste wartości
    return [...new Set(values.filter(v => v && v.trim() !== ""))]
        .sort((a, b) => a.localeCompare(b, "pl"));  // sortowanie wg polskich znaków
}


//resetuje pola wyboru na domyślne wartości
function resetSelect(select, placeholder) {
    select.innerHTML = "";
    const option = document.createElement("option");
    option.value = "";
    option.textContent = placeholder;
    select.appendChild(option);
}

function clearStats() {  // czyści wartości min i max
    minValue.textContent = "-";
    maxValue.textContent = "-";
}

function clearChart() {  // czyści wykres
    if (measurementChart) {
        measurementChart.destroy();
        measurementChart = null;
    }
}

function clearAvailableMeasurements() {  // czyści listę dostępnych rodzaji pomiarów
    if (measurementsList) {
        measurementsList.textContent = "Brak wybranej stacji";
    }
}

function clearMeasurementView() {  // reset stanu pomiarów, czyści interfejs (minmax/wykres/lista pomiarów)
    appState.selectedSensor = null;
    appState.selectedRange = null;
    appState.sensors = [];
    appState.measurements = [];

    clearStats();
    clearChart();
    clearAvailableMeasurements();
}

function refreshDisplayedData() { // sprawdza czy wybrano wymagane opcje, jeśli nie -> czyści wyświetlane dane
    if (!appState.selectedStation || !appState.selectedSensor || !appState.selectedRange) {
        clearStats();
        clearChart();
        return;
    }
//filtr danych pomiarowych wg zakresu
    const filtered = filterByRange(appState.measurements, appState.selectedRange);

    console.log("Po filtrze:", filtered);

    const validValues = filtered
        .map(item => item.value)
        .filter(value =>
            value !== null &&
            value !== undefined &&
            !Number.isNaN(value)
        );

    if (validValues.length === 0) {  // jeżeli brak dostępnych danych, czyści minmax i wykres
        clearStats();
        clearChart();
        return;
    }

    //obliczanie i wyświetlanie min max
    const min = Math.min(...validValues);
    const max = Math.max(...validValues);

    minValue.textContent = min.toFixed(2);
    maxValue.textContent = max.toFixed(2);


    //dane do wykresu
    drawChart(filtered);
}

function parseGiosDate(dateText) {
    if (!dateText) return null;

    // format GIOŚ: "2026-04-24 10:00:00" -> "2026-04-24T10:00:00"
    return new Date(dateText.replace(" ", "T"));
}

function filterByRange(data, range) {
    const now = new Date();
    const cutoff = new Date(now.getTime());


    // wyfiltrowuje zakres czasowy
    if (range === "24h") {
        cutoff.setHours(cutoff.getHours() - 24);
    } else if (range === "3d") {
        cutoff.setDate(cutoff.getDate() - 3);
    } else if (range === "7d") {
        cutoff.setDate(cutoff.getDate() - 7);
    } else if (range === "21d") {
        cutoff.setDate(cutoff.getDate() - 21);
    } else {
        return data;
    }


    // wyfiltrowuje dane bez poprawnej daty
    return data.filter(item => {
        const measurementDate = parseGiosDate(item.date);

        if (!measurementDate || isNaN(measurementDate.getTime())) {
            return false;
        }

        return measurementDate >= cutoff && measurementDate <= now;
    });
}


// rysuje wykres
function drawChart(data) {
    clearChart();

    const chartData = data
        .filter(item => item.value !== null && item.value !== undefined && !Number.isNaN(item.value))
        .sort((a, b) => parseGiosDate(a.date) - parseGiosDate(b.date));

    if (chartData.length === 0) return;

    const labels = chartData.map(item => item.date);
    const values = chartData.map(item => item.value);

    measurementChart = new Chart(chartCanvas, {
        type: "line",
        data: {
            labels: labels,
            datasets: [{
                label: "Wartość pomiaru",
                data: values,
                tension: 0.2,
                pointRadius: 3
            }]
        },
        options: {
            responsive: true,
            scales: {
                x: {
                    ticks: {
                        maxRotation: 45,
                        minRotation: 45
                    }
                },
                y: {
                    beginAtZero: false
                }
            }
        }
    });
}

//zmiana zakresu czasu, odświeża min/max i wykres
rangeSelect.addEventListener("change", function () {
    appState.selectedRange = rangeSelect.value || null;
    refreshDisplayedData();
});

//pobranie listy sensorów wybranej stacji, wyświetlanie ich, wypełnia pole wyboru pomiaru
function loadAvailableMeasurementsForSelectedStation() {
    if (!measurementsList || !appState.selectedStation) return;

    const stationId = appState.selectedStation["Identyfikator stacji"];

    fetch(`/api/sensors?stationId=${stationId}`)
        .then(response => {
            if (!response.ok) {
                throw new Error("Nie udalo sie pobrac sensorow");
            }
            return response.json();
        })
        .then(data => {
            const sensors = data["Lista stanowisk pomiarowych"] || [];
            appState.sensors = sensors;

            const measurementNames = sensors
                .map(sensor => {
                    const name = sensor["Wskaźnik"] || "";
                    const code = sensor["Wskaźnik - kod"] || "";
                    return name && code ? `${name} (${code})` : name || code;
                })
                .filter(v => v && v.trim() !== "");

            measurementsList.textContent = measurementNames.length > 0
                ? measurementNames.join(", ")
                : "Brak dostępnych pomiarów";

            resetSelect(sensorSelect, "Wybierz pomiar");

            sensors.forEach(sensor => {
                const sensorId = sensor["Identyfikator stanowiska"];
                const name = sensor["Wskaźnik"] || "";
                const code = sensor["Wskaźnik - kod"] || "";

                const option = document.createElement("option");
                option.value = sensorId;
                option.textContent = name && code ? `${name} (${code})` : name || code;

                sensorSelect.appendChild(option);
            });

            sensorSelect.disabled = sensors.length === 0;
        })
        .catch(error => {
            console.error(error);
            measurementsList.textContent = "Błąd pobierania pomiarów";
            resetSelect(sensorSelect, "Wybierz pomiar");
            sensorSelect.disabled = true;
        });
}

//pobiera sensory wybranej stacji, diagnozuje strukturę odpowiedzi, wyświetla dostępne pomiary, odblokowyje listę wyboru sensora
function loadAvailableMeasurementsForSelectedStation() {
    if (!measurementsList || !appState.selectedStation) return;

    const stationId = appState.selectedStation["Identyfikator stacji"];

    measurementsList.textContent = "Pobieranie danych...";

    fetch(`/api/sensors?stationId=${stationId}`)
        .then(response => {
            if (!response.ok) {
                throw new Error("Nie udalo sie pobrac sensorow");
            }
            return response.json();
        })
        .then(data => {
            console.log("Odpowiedz /api/sensors:", data);

            let sensors = [];

            if (Array.isArray(data["Lista stanowisk pomiarowych"])) {
                sensors = data["Lista stanowisk pomiarowych"];
            } else {
                for (const key in data) {
                    if (Array.isArray(data[key])) {
                        sensors = data[key];
                        break;
                    }
                }
            }

            console.log("Rozpoznane sensory:", sensors);

            appState.sensors = sensors;

            const measurementNames = sensors
                .map(sensor => {
                    const name = sensor["Wskaźnik"] || "";
                    const code =
                        sensor["Wskaźnik - kod"] ||
                        sensor["Kod wskaźnika"] ||
                        "";

                    if (name && code) return `${name} (${code})`;
                    return name || code;
                })
                .filter(value => value && value.trim() !== "");

            measurementsList.textContent = measurementNames.length > 0
                ? measurementNames.join(", ")
                : "Brak dostępnych pomiarów";

            resetSelect(sensorSelect, "Wybierz pomiar");

            sensors.forEach(sensor => {
                const sensorId = sensor["Identyfikator stanowiska"];
                const name = sensor["Wskaźnik"] || "";
                const code =
                    sensor["Wskaźnik - kod"] ||
                    sensor["Kod wskaźnika"] ||
                    "";

                const option = document.createElement("option");
                option.value = sensorId;
                option.textContent = name && code ? `${name} (${code})` : name || code;

                sensorSelect.appendChild(option);
            });

            sensorSelect.disabled = sensors.length === 0;
        })
        .catch(error => {
            console.error(error);
            measurementsList.textContent = "Błąd pobierania pomiarów";
            resetSelect(sensorSelect, "Wybierz pomiar");
            sensorSelect.disabled = true;
        });
}

// wczytuje historię pomiarów z pliku, wybiera dane dla konkretnej stacji i sensora, zapisuje je w stanie aplikacji, odświeża wyświetlane wyniki
function loadMeasurementsHistoryForSelectedSensor() {
    if (!appState.selectedStation || !appState.selectedSensor) return;

    fetch("measurements_history.json")
        .then(response => {
            if (!response.ok) {
                throw new Error("Nie udalo sie wczytac measurements_history.json");
            }
            return response.json();
        })
        .then(data => {
            const history = data.measurements || [];

            const stationId = appState.selectedStation["Identyfikator stacji"];
            const sensorId = appState.selectedSensor;

            const entry = history.find(item =>
                item.stationId === stationId &&
                item.sensorId === sensorId
            );

            if (!entry || !Array.isArray(entry.values)) {
                appState.measurements = [];
                refreshDisplayedData();
                return;
            }

            appState.measurements = entry.values
                .filter(item => item.date)
                .map(item => ({
                    date: item.date,
                    value: item.value === null ? null : Number(item.value)
                }));

            refreshDisplayedData();
        })
        .catch(error => {
            console.error(error);
            appState.measurements = [];
            refreshDisplayedData();
        });
}

// obsługuje wybór sensora, pobiera dla niego dane z serwera, zapisuje je do historii, wyświetla wyniki
sensorSelect.addEventListener("change", function () {
    if (!sensorSelect.value) {
        appState.selectedSensor = null;
        appState.measurements = [];
        clearStats();
        clearChart();
        return;
    }

    appState.selectedSensor = parseInt(sensorSelect.value);

    const stationId = appState.selectedStation["Identyfikator stacji"];
    const sensorId = appState.selectedSensor;

    console.log("Pobieram pomiar:", {
        stationId: stationId,
        sensorId: sensorId
    });

    fetch(`/api/measure?stationId=${stationId}&sensorId=${sensorId}`)
        .then(response => {
            if (!response.ok) {
                throw new Error("Nie udalo sie pobrac danych pomiarowych");
            }
            return response.json();
        })
        .then(data => {
            console.log("Pobrane dane pomiarowe:", data);

            return loadMeasurementsHistoryForSelectedSensor();
        })
        .catch(error => {
            console.error(error);
            appState.measurements = [];
            clearStats();
            clearChart();
        });
});

// tworzy i wypełnia listę wyboru województw
function fillProvinces() {
    const provinces = uniqueSorted(
        stations.map(station => station["Województwo"] || "")
    );

    resetSelect(provinceSelect, "Województwo");

    provinces.forEach(province => {
        const option = document.createElement("option");
        option.value = province;
        option.textContent = province;
        provinceSelect.appendChild(option);
    });
}

// obsługuję zmianę województwa, resetuje niższe pola
provinceSelect.addEventListener("change", function () {
    resetSelect(districtSelect, "Powiat");
    resetSelect(communeSelect, "Gmina");
    resetSelect(stationSelect, "Stacja");

    districtSelect.disabled = true;
    communeSelect.disabled = true;
    stationSelect.disabled = true;

    output.textContent = "Brak wyboru";
    appState.selectedStation = null;
    clearMeasurementView();

    const selectedProvince = provinceSelect.value;
    if (!selectedProvince) return;

    const districts = uniqueSorted(
        stations
            .filter(station => station["Województwo"] === selectedProvince)
            .map(station => station["Powiat"] || "")
    );

    districts.forEach(district => {
        const option = document.createElement("option");
        option.value = district;
        option.textContent = district;
        districtSelect.appendChild(option);
    });

    districtSelect.disabled = false;
});

// obsługuję zmianę powiatu, resetuje niższe pola
districtSelect.addEventListener("change", function () {
    resetSelect(communeSelect, "Gmina");
    resetSelect(stationSelect, "Stacja");

    communeSelect.disabled = true;
    stationSelect.disabled = true;

    output.textContent = "Brak wyboru";
    appState.selectedStation = null;
    clearMeasurementView();

    const selectedProvince = provinceSelect.value;
    const selectedDistrict = districtSelect.value;
    if (!selectedProvince || !selectedDistrict) return;

    const communes = uniqueSorted(
        stations
            .filter(station =>
                station["Województwo"] === selectedProvince &&
                station["Powiat"] === selectedDistrict
            )
            .map(station => station["Gmina"] || "")
    );

    communes.forEach(commune => {
        const option = document.createElement("option");
        option.value = commune;
        option.textContent = commune;
        communeSelect.appendChild(option);
    });

    communeSelect.disabled = false;
});

// obsługuję zmianę gminy, resetuje niższe pola
communeSelect.addEventListener("change", function () {
    resetSelect(stationSelect, "Stacja");

    stationSelect.disabled = true;

    output.textContent = "Brak wyboru";
    appState.selectedStation = null;
    clearMeasurementView();

    const selectedProvince = provinceSelect.value;
    const selectedDistrict = districtSelect.value;
    const selectedCommune = communeSelect.value;

    if (!selectedProvince || !selectedDistrict || !selectedCommune) return;

    const filteredStations = stations.filter(station =>
        station["Województwo"] === selectedProvince &&
        station["Powiat"] === selectedDistrict &&
        station["Gmina"] === selectedCommune
    );

    filteredStations.forEach(station => {
        const option = document.createElement("option");

        const label =
            station["Ulica"] && station["Ulica"] !== "Brak ulicy"
                ? station["Ulica"]
                : station["Nazwa stacji"];

        option.textContent = label;
        option.value = JSON.stringify(station);

        stationSelect.appendChild(option);
    });

    stationSelect.disabled = false;
});

// obsługuję zmianę adresu stacji, resetuje niższe pola
stationSelect.addEventListener("change", function () {
    if (!stationSelect.value) {
        output.textContent = "Brak wyboru";
        appState.selectedStation = null;
        clearMeasurementView();
        return;
    }

    const station = JSON.parse(stationSelect.value);
    appState.selectedStation = station;
    clearMeasurementView();

    const lastPart =
        station["Ulica"] && station["Ulica"] !== "Brak ulicy"
            ? station["Ulica"]
            : station["Nazwa stacji"];

    const fullAddress = [
        station["Województwo"],
        station["Powiat"],
        station["Gmina"],
        lastPart
    ]
        .filter(part => part && part.trim() !== "")
        .join(", ");

    output.textContent = fullAddress;
    console.log("Wybrana stacja:", appState.selectedStation);

    loadAvailableMeasurementsForSelectedStation();


});

// wybór sensora, zapis id w stanie aplikacji,wypisywanie go w konsoli
sensorSelect.addEventListener("change", function () {
    if (!sensorSelect.value) {
        appState.selectedSensor = null;
        return;
    }

    appState.selectedSensor = parseInt(sensorSelect.value);
    console.log("Wybrany sensor ID:", appState.selectedSensor);
});