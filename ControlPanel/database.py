from flask_sqlalchemy import SQLAlchemy
from sqlalchemy.dialects.mysql import DECIMAL

# Inicjalizacja bazy danych
db = SQLAlchemy()
threshold_number_measurements = 250

# Tabela z typami sensorów
class SensorsTypes(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    name = db.Column(db.String(50), unique=True, nullable=False)  # Nazwa sensora, np. 'Sterowanie światłem', 'Czujnik temperatury'


# Tabela z sensorami
class Sensors(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    type_id = db.Column(db.Integer, db.ForeignKey('sensors_types.id'), nullable=False)  # Typ sensora (klucz obcy do sensor_types)
    group = db.Column(db.Integer, nullable=False)  # Grupa sensora, np. 1, 2, 3

    # Relacje
    type = db.relationship('SensorsTypes', backref=db.backref('sensors', lazy=True))  # Powiązanie z tabelą sensor_types


# Tabela z aktualnym stanem sensorów (np. dla światła i wentylatora)
class SensorsStates(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    sensor_id = db.Column(db.Integer, db.ForeignKey('sensors.id'), nullable=False)  # Klucz obcy do sensorów
    state = db.Column(db.String(4), nullable=False)  # Stan sensora: 0 lub 1
    
    # Relacje
    sensor = db.relationship('Sensors', backref=db.backref('states', lazy=True))



class Measurements(db.Model):
    date = db.Column(db.DateTime, primary_key=True, nullable=False)  # Data i czas pomiaru
    sensor_id = db.Column(db.Integer, db.ForeignKey('sensors.id'), nullable=False)  # Klucz obcy do sensorów
    value = db.Column(DECIMAL(10, 2), nullable=False)  # Wartość pomiaru z dokładnością 2 miejsc po przecinku

    # Relacje
    sensor = db.relationship('Sensors', backref=db.backref('measurements', lazy=True))


# Funkcja do aktualizacji stanu sensora
def update_sensor_state(sensor_id, state):
    try:
        sensor_state = SensorsStates.query.filter_by(sensor_id=sensor_id).first()
        if sensor_state:
            # Jeśli stan już istnieje, zaktualizuj go
            sensor_state.state = state
        else:
            # Jeśli stan nie istnieje, utwórz nowy wpis
            sensor_state = SensorsStates(sensor_id=sensor_id, state=state)
            db.session.add(sensor_state)
        db.session.commit()
    except Exception as e:
        db.session.rollback()
        print(f"Error updating sensor state: {e}")


# Funkcja zwracająca wszystkie bieżace stany czujników on/off. 
def get_all_states():
    return SensorsStates.query.all()


def add_measurement(sensor_id, measurement_date, value):
    try:    
        # Dodaj nowy pomiar
        new_measurement = Measurements(sensor_id=sensor_id, date=measurement_date, value=value)
        db.session.add(new_measurement)
        db.session.commit()

        # Sprawdź liczbę pomiarów dla danego czujnika
        measurements = Measurements.query.filter_by(sensor_id=sensor_id).order_by(Measurements.date).all()
        if len(measurements) > threshold_number_measurements:
            # Usuń najstarsze pomiary, jeśli liczba przekracza threshold_number_measurements
            for measurement in measurements[:-threshold_number_measurements]:
                db.session.delete(measurement)
            db.session.commit()
    except Exception as e:
        db.session.rollback()
        print(f"Error adding measurement: {e}")
