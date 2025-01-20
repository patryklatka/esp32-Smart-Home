Smart home control system project


This project shows the implementation of a smart home control system using ESP32 microcontrollers. Thread protocol was used for communication between microcontrollers. A custom border router was implemented, which acts as a gateway between the local network and the Internet. The control panel communicates with the local network using the MQTT protocol. The backend of the control panel was written using the Flask framework, and the technologies used in the frontend are HTML, CSS and JavaScript. The backend of the panel handles the database. Figure 1 shows the general scheme of communication in the system. 

<div style="text-align: center">
  <img src="images/11_architektura_systemu.png" alt="Schemat projektu" width="200">
  <p><em>Img. 1: Scheme of the system</em></p>
</div>