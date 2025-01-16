// ui.js
// Funkcja do zmiany widoku na wykres temperatury
$('#gr1_temperatureButton').on('click', function() {
    $('#mainMenu').removeClass('visible').addClass('hidden');
    $('#gr1_temperaturePlotSection').removeClass('hidden').addClass('visible');
    // createPlot();  // Tworzymy wykres
});

// Funkcja do powrotu do menu głównego
$('#gr1_backToMenuButtonFromTemperature').on('click', function() {
    $('#gr1_temperaturePlotSection').removeClass('visible').addClass('hidden');
    $('#gr1_humidityPlotSection').removeClass('visible').addClass('hidden');
    $('#mainMenu').removeClass('hidden').addClass('visible');
});

// Zaktualizowanie temperatury (stała wartość na razie)
function updateTemperatureDisplay(value) {
    $('#gr1_temperatureDisplay').text(value);
}


// Funkcja do zmiany widoku na wykres wilgotności
$('#gr1_humidityButton').on('click', function() {
    $('#mainMenu').removeClass('visible').addClass('hidden');
    $('#gr1_humidityPlotSection').removeClass('hidden').addClass('visible');
    // createHumidityPlot();  // Tworzymy wykres
});

// Funkcja do powrotu do menu głównego
$('#gr1_backToMenuButtonFromHumidity').on('click', function() {
    $('#gr1_humidityPlotSection').removeClass('visible').addClass('hidden');
    $('#gr1_temperaturePlotSection').removeClass('visible').addClass('hidden');
    $('#mainMenu').removeClass('hidden').addClass('visible');
});

// Zaktualizowanie wilgotności
function updateHumidityDisplay(value) {
    $('#gr1_humidityDisplay').text(value);
}


/*
Brak uniwersalności kodu. 
Opieram się na kolejności elementów w bazie zgodnie z: ["Czujnik światła", "Wiatrak", "Czujnik temperatury", "Czujnik wilogtności"]
                                    Odpowiednio ID:             1               2               3                   4

                    KONIECZNIE DO POPRAWY
*/
// Aktualizacja frontu po załadowaniu strony
function updateUIStates(states) {
    states.forEach((state) => {

        // Aktualizacja stanu w interfejsie
        if (state.sensor_id == 1){
            handleLightState({'state': state.state})
        }
        else if (state.sensor_id == 2){
            handleFanState({'state': state.state})
        }
        
    });
}


// // Nasłuchujemy na zmianę rozmiaru okna
// $(window).resize(function() {
//     Plotly.relayout('gr1_temperaturePlot', {
//         width: $('#gr1_temperaturePlot').width(),
//         height: $('#gr1_temperaturePlot').height()
//     });
// });
