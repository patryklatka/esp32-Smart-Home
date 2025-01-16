from gevent import monkey
monkey.patch_all()
from datetime import datetime, timedelta
from flask import Flask, render_template
from flask_socketio import SocketIO, emit
import paho.mqtt.client as paho
import os
from flask_mqtt import Mqtt
import pymysql
import database
pymysql.install_as_MySQLdb()
from dotenv import load_dotenv
load_dotenv()

app = Flask(__name__)
socketio = SocketIO(app, async_mode='gevent')

# Konfiguracja bazy danych MySQL (freemysqlhosting)
app.config['SQLALCHEMY_DATABASE_URI'] = os.getenv('SQLALCHEMY_DATABASE_URI')
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False
app.config['SQLALCHEMY_ENGINE_OPTIONS'] = {
    'pool_recycle': 280,  # Odświeżaj połączenie co 280 sekund
    'pool_pre_ping': True,  # Pinguj serwer przed użyciem połączenia
    'connect_args': {
        'connect_timeout': 30  # Maksymalny timeout połączenia
    }
}
database.db.init_app(app)

# Tworzenie tabeli w bazie (tylko przy pierwszym uruchomieniu)
with app.app_context():
    database.db.create_all()  

# Konfiguracja MQTT
app.config['MQTT_BROKER_URL'] = os.getenv('MQTT_BROKER_URL')
app.config['MQTT_BROKER_PORT'] = 8883
app.config['MQTT_USERNAME'] = os.getenv('MQTT_USERNAME')
app.config['MQTT_PASSWORD'] = os.getenv('MQTT_PASSWORD')
app.config['MQTT_TLS_ENABLED'] = True
app.config['MQTT_KEEP_ALIVE'] = 60
app.config['MQTT_TLS_VERSION'] = paho.ssl.PROTOCOL_TLS  

mqtt = Mqtt(app)

# Tematy MQTT
gr1_temperature_topic = "gr1/temperature"
gr1_light_topic = "gr1/swiatlo"
gr1_fan_topic = "gr1/wiatrak"
gr1_humidity_topic = "gr1/wilgotnosc"

gr1_light_topic_ui = "gr1_ui/swiatlo"
gr1_fan_topic_ui = "gr1_ui/wiatrak"

# Obsługa połączenia MQTT
@mqtt.on_connect()
def handle_connect(client, userdata, flags, rc):
    print(f"Connected to MQTT broker with result code {rc}")
    if rc == 0:
        mqtt.subscribe(gr1_temperature_topic)  
        mqtt.subscribe(gr1_light_topic)  
        mqtt.subscribe(gr1_fan_topic)  
        mqtt.subscribe(gr1_humidity_topic)  
    else:
        print(f"Connection failed with error code {rc}")

# Obsługa wiadomości MQTT
@mqtt.on_message()
def handle_message(client, userdata, message):
    topic = message.topic 
    payload = message.payload.decode()  
    print(f"Odebrano wiadomość na temacie '{topic}': {payload}")

    if topic == gr1_temperature_topic:  # Obsługa temperatury
        measurement_date = (datetime.now() + timedelta(hours=1, seconds=10)).replace(microsecond=0).isoformat()
        temperature = payload
        print(f"Odebrana temperatura to {temperature} i czas {measurement_date}")

        with app.app_context():
            socketio.emit('gr1_new_temperature_data', {'x': measurement_date.split("T")[1][:5], 'y': temperature})
            database.add_measurement(3, measurement_date, temperature)

    elif topic == gr1_light_topic:  
        if payload in ['on', 'off']:
            state = 'włączono' if payload == 'on' else 'wyłączono'
            print(f"Światło zostało {state}")

            with app.app_context():
                socketio.emit('gr1_light_state', {'state': payload})
                database.update_sensor_state(1, payload) 
                """
                Brak uniwersalności kodu. 
                Opieram się na kolejności elementów w bazie zgodnie z: ["Czujnik światła", "Wiatrak", "Czujnik temperatury", "Czujnik wilogtności"]
                                                    Odpowiednio ID:             1               2               3                   4
                KONIECZNIE DO POPRAWY
                """

    elif topic == gr1_fan_topic:  
        if payload in ['on', 'off']:
            state = 'włączony' if payload == 'on' else 'wyłączony'
            print(f"Wiatrak został {state}")

            with app.app_context():
                socketio.emit('gr1_fan_state', {'state': payload})
                database.update_sensor_state(2, payload)
                """
                Brak uniwersalności kodu. 
                Opieram się na kolejności elementów w bazie zgodnie z: ["Czujnik światła", "Wiatrak", "Czujnik temperatury", "Czujnik wilogtności"]
                                                Odpowiednio ID:             1               2               3                   4
                KONIECZNIE DO POPRAWY
                """

    elif topic == gr1_humidity_topic:  
        measurement_date = (datetime.now() + timedelta(hours=1)).replace(microsecond=0).isoformat()
        humidity = payload
        print(f"Odebrana wilgotnosc to {humidity} i czas {measurement_date}")

        with app.app_context():
            socketio.emit('gr1_new_humidity_data', {'x': measurement_date, 'y': humidity})
            database.add_measurement(4, measurement_date, humidity)


# Funkcja do obsługi komendy włącz/wyłącz światło
@socketio.on('gr1_light_command')
def handle_light_command(data):
    command = data['command']
    if command in ['on', 'off']:
        mqtt.publish(gr1_light_topic_ui, command)
        print(f"Wysłano komendę {command} do tematu {gr1_light_topic}")

# Funkcja do obsługi komendy włącz/wyłącz wiatraka
@socketio.on('gr1_fan_command')
def handle_fan_command(data):
    command = data['command']
    if command in ['on', 'off']:
        mqtt.publish(gr1_fan_topic_ui, command)
        print(f"Wysłano komendę {command} do tematu {gr1_fan_topic}")

# Inicjalne wysyłanie stanów sensorów do klienta
@socketio.on('get_states')
def send_states():
    try:
        states = database.SensorsStates.query.all()
        states_list = [
            {'sensor_id': state.sensor_id, 'state': state.state}
            for state in states
        ]
        # Emitowanie do klienta
        print("Stany po odświeżeniu:", states_list)
        emit('initial_states', states_list)
    except Exception as e:
        print(f"Error fetching states: {e}")


# Funkcja do wysyłania danych wykresu przy połączeniu WebSocket
@socketio.on('connect')
def send_initial_chart_data():
    # Pobranie danych z bazy dla temperatury
    temp_measurements = database.Measurements.query.filter_by(sensor_id=3).order_by(database.Measurements.date).all()
    data = [{"x": temp.date.isoformat(), "y": float(temp.value)} for temp in temp_measurements]
    print("initial temp data", data)
    # Wysłanie danych do klienta
    socketio.emit('initial_temp_chart_data', data)

    humidity_measurements = database.Measurements.query.filter_by(sensor_id=4).order_by(database.Measurements.date).all()
    data = [{"x": humidity.date.isoformat(), "y": float(humidity.value)} for humidity in humidity_measurements]
    print("initial humidity", data)
    # Wysłanie danych do klienta
    socketio.emit('initial_humidity_chart_data', data)

@app.route('/')
def menu():
    return render_template('menu.html')
