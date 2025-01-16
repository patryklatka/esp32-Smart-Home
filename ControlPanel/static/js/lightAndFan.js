// lightAndFan.js
var lightState = 'off'; // Stan światła
var fanState = 'off'; // Stan wiatraka

// Funkcja do obsługi stanu światła
function handleLightState(data) {
    if (data && data.state) {
        console.log("Stan światła:", data.state);
        if (data.state === 'on') {
            $('#gr1_lightButton').text('Wyłącz światło');
            $('#gr1_lightIndicator').css('background-color', 'yellow');
            lightState = 'on';
        } else if (data.state === 'off') {
            $('#gr1_lightButton').text('Włącz światło');
            $('#gr1_lightIndicator').css('background-color', 'black');
            lightState = 'off';
        }
    }
}

// Funkcja do obsługi stanu wiatraka
function handleFanState(data) {
    if (data && data.state) {
        console.log("Stan wiatraka:", data.state);
        if (data.state === 'on') {
            $('#gr1_fanButton').text('Wyłącz wiatrak');
            $('#gr1_fanIndicator').css({
                'background-color': '#FFC107',
                'animation': 'spin 1s linear infinite'
            });
            fanState = 'on';
        } else if (data.state === 'off') {
            $('#gr1_fanButton').text('Włącz wiatrak');
            $('#gr1_fanIndicator').css({
                'background-color': '#4CAF50',
                'animation': 'none'
            });
            fanState = 'off';
        }
    }
}

// Obsługuje kliknięcie w przycisk "Włącz/wyłącz światło"
$('#gr1_lightButton').on('click', function() {
    if (lightState === 'off') {
        socket.emit('gr1_light_command', { command: 'on' });
        $('#gr1_lightButton').text('Wyłącz światło');
        $('#gr1_lightIndicator').css('background-color', 'black');
        lightState = 'on';
    } else {
        socket.emit('gr1_light_command', { command: 'off' });
        $('#gr1_lightButton').text('Włącz światło');
        $('#gr1_lightIndicator').css('background-color', 'yellow');
        lightState = 'off';
    }
});

// Obsługuje kliknięcie w przycisk "Włącz/wyłącz wiatrak"
$('#gr1_fanButton').on('click', function() {
    if (fanState === 'off') {
        socket.emit('gr1_fan_command', { command: 'on' });
        $('#gr1_fanButton').text('Wyłącz wiatrak');
        fanState = 'on';
        $('#gr1_fanIndicator').css({
            'background-color': '#FFC107',
            'animation': 'spin 1s linear infinite'
        });
    } else {
        socket.emit('gr1_fan_command', { command: 'off' });
        $('#gr1_fanButton').text('Włącz wiatrak');
        fanState = 'off';
        $('#gr1_fanIndicator').css({
            'background-color': '#4CAF50',
            'animation': 'none'
        });
    }
});
