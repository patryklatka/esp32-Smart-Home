// websocket.js
var socket = io.connect(document.domain + ':' + location.port);
var number_plot_temp_points = 200
var number_plot_humidity_points = 200

socket.on('connect', function() {
    console.log('Połączono z WebSocket!');
});

// Nasłuchujemy na dane przychodzące z WebSocket
socket.on('gr1_new_temperature_data', function(data) {
    console.log("Otrzymano nowe dane:", data);
    updateTemperatureDisplay(data.y + '°C')

    if (data && data.x !== undefined && data.y !== undefined) {
        // Dodajemy nowy punkt do wykresu
        Plotly.extendTraces('gr1_temperaturePlot', {
            x: [[data.x]],
            y: [[data.y]]
        }, [0], number_plot_temp_points);
    }
});

// Nasłuchujemy na wiadomości dotyczące światła
socket.on('gr1_light_state', function(data) {
    console.log('Initial 2 render width:', $('#gr1_temperaturePlot').width())
    handleLightState(data);
});

// Nasłuchujemy na wiadomości dotyczące wiatraka
socket.on('gr1_fan_state', function(data) {
    handleFanState(data);
});

// Nasłuchujemy na dane wilgotności
socket.on('gr1_new_humidity_data', function(data) {
    console.log("Otrzymano nowe dane wilgotności:", data);
    updateHumidityDisplay(data.y + '%')

    if (data && data.x !== undefined && data.y !== undefined) {
        Plotly.extendTraces('gr1_humidityPlot', {
            x: [[data.x]],
            y: [[data.y]]
        }, [0], number_plot_humidity_points);
    }
});

// Obsługa pobierania stanów z serwera po załadowaniu strony.
$(document).ready(function () {
    // Po nawiązaniu połączenia WebSocket
    socket.on('connect', function () {
        // Wysyłanie żądania pobrania stanów
        socket.emit('get_states');
    });

    // Odbieranie inicjalnych stanów
    socket.on('initial_states', function (data) {
        console.log('Initial states received:', data);
        updateUIStates(data);
    });


    // Nasłuch na event 'initial_chart_data', który przyjdzie z serwera
    socket.on('initial_temp_chart_data', function(data) {
        console.log('Initial temp chart data received:', data);

        const xValues = data.map(item => item.x);
        const yValues = data.map(item => item.y);
        
        // aktualizuj wysietlany ostatni pomiar
        updateTemperatureDisplay(parseFloat(yValues[yValues.length - 1]).toFixed(2) + '°C')

        // Zainicjowanie danych wykresu
        const chartData = [{
            x: xValues,
            y: yValues,
            type: 'scatter',
            mode: 'lines+markers'
        }];

        // Layout wykresu
        const layout = {
            title: 'Temperatura',
            xaxis: { title: 'Data pomiaru' },
            yaxis: { title: 'Temperatura (°C)' }
        };

        // Funkcja do renderowania wykresu po upewnieniu się, że szerokość jest prawidłowa
        function renderPlot() {
            const width = $('#gr1_temperaturePlot').width();
            if (width > 0) {
                Plotly.newPlot('gr1_temperaturePlot', chartData, layout).then(function () {
                    // Wymuszenie przeskalowania wykresu po jego załadowaniu
                    Plotly.relayout('gr1_temperaturePlot', {
                        width: $('#gr1_temperaturePlot').width(),
                        height: $('#gr1_temperaturePlot').height()
                    });
                });
            } else {
                console.log('Retrying render in 100ms...');
                setTimeout(renderPlot, 100); // Powtórz próbę za 100 ms
            }
        }

        // Wywołanie funkcji renderowania
        renderPlot();
    });


    socket.on('initial_humidity_chart_data', function(data) {
        console.log('Initial humidity chart data received:', data);

        const xValues = data.map(item => item.x);
        const yValues = data.map(item => item.y);
        
        // aktualizuj wysietlany ostatni pomiar
        updateHumidityDisplay(parseFloat(yValues[yValues.length - 1]).toFixed(2)  + '%')

        // Zainicjowanie danych wykresu
        const chartData = [{
            x: xValues,
            y: yValues,
            type: 'scatter',
            mode: 'lines+markers'
        }];

        // Layout wykresu
        const layout = {
            title: 'Wilgotność',
            xaxis: { title: 'Data pomiaru' },
            yaxis: { title: 'Wilgotność (%)' }
        };

        // Funkcja do renderowania wykresu po upewnieniu się, że szerokość jest prawidłowa
        function renderPlot() {
            const width = $('#gr1_humidityPlot').width();
            if (width > 0) {
                Plotly.newPlot('gr1_humidityPlot', chartData, layout).then(function () {
                    // Wymuszenie przeskalowania wykresu po jego załadowaniu
                    Plotly.relayout('gr1_humidityPlot', {
                        width: $('#gr1_humidityPlot').width(),
                        height: $('#gr1_humidityPlot').height()
                    });
                });
            } else {
                console.log('Retrying render in 100ms...');
                setTimeout(renderPlot, 100); // Powtórz próbę za 100 ms
            }
        }

        // Wywołanie funkcji renderowania
        renderPlot();
    });
});

// Nasłuchujemy na zmianę rozmiaru okna
$(window).resize(function() {
    console.log('Initial 3 render width:', $('#gr1_temperaturePlot').width())
    Plotly.relayout('gr1_temperaturePlot', {
        width: $('#gr1_temperaturePlot').width(),
        height: $('#gr1_temperaturePlot').height()
    });
    console.log('Initial 33 render width:', $('#gr1_humidityPlot').width())
    Plotly.relayout('gr1_humidityPlot', {
        width: $('#gr1_humidityPlot').width(),
        height: $('#gr1_humidityPlot').height()
    });
});
