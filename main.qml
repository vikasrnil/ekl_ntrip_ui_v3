import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    visible: true
    width: 800
    height: 480
    title: "NTRIP Client"

    property bool isConnected: false

    // ===== BACKGROUND =====
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#1e3c72" }
            GradientStop { position: 1.0; color: "#2a5298" }
        }
    }

    // ================= TIMER =================
    Timer {
        id: clearTimer
        interval: 2000
        repeat: false
        onTriggered: statusMsg.text = ""
    }

    function showMessage(msg) {
        statusMsg.text = msg
        clearTimer.restart()
    }

    // ================= MAIN PAGE =================
    Item {
        anchors.fill: parent
        visible: !isConnected

        RowLayout {
            anchors.fill: parent
            anchors.margins: 15
            spacing: 15

            // ===== LEFT PANEL =====
            Rectangle {
                Layout.fillHeight: true
                Layout.fillWidth: true
                radius: 12
                color: "white"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 10

                    Text {
                        text: "NTRIP CLIENT"
                        font.pixelSize: 22
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }

                    TextField { id: hostField; placeholderText: "Host"; Layout.fillWidth: true }
                    TextField { id: portField; placeholderText: "Port"; Layout.fillWidth: true }

                    ComboBox {
                        id: mountCombo
                        model: []
                        Layout.fillWidth: true
                    }

                    TextField { id: userField; placeholderText: "User"; Layout.fillWidth: true }
                    TextField {
                        id: passwordField
                        placeholderText: "Password"
                        echoMode: TextInput.Password
                        Layout.fillWidth: true
                    }

                    RowLayout {
                        spacing: 10
                        CheckBox {
                            id: ggaCheck
                            text: "Send GGA"
                            onCheckedChanged: {
                                ntripClient.setUseFileGGA(checked)
                            }
                        }
                    }
                }
            }

            // ===== RIGHT PANEL  =====
		Rectangle {
		    Layout.preferredWidth: 250
		    Layout.fillHeight: false
		    Layout.alignment: Qt.AlignVCenter
		    height: 300
		    radius: 12
		    color: "#DDECFF"

		    ColumnLayout {
			anchors.fill: parent
			anchors.margins: 20

			// Top Spacer
			Item { Layout.fillHeight: true }

			Text {
			    id: statusMsg
			    text: ""
			    color: "#ff9800"
			    horizontalAlignment: Text.AlignHCenter
			    Layout.fillWidth: true
			}

			Button {
			    text: "Fetch"
			    Layout.fillWidth: true
			    Layout.preferredHeight: 50
			    background: Rectangle { color: "#4CAF50"; radius: 10 }

			    onClicked: {
				if (hostField.text === "" || portField.text === "") {
				    showMessage("Enter Host & Port")
				    return
				}
				showMessage("Fetching...")
				ntripClient.fetchMountPoints(
				    hostField.text,
				    parseInt(portField.text)
				)
			    }
			}

			Button {
			    text: "Connect"
			    Layout.fillWidth: true
			    Layout.preferredHeight: 50
			    background: Rectangle { color: "#2196F3"; radius: 10 }

			    onClicked: {
				if (hostField.text === "" ||
				    portField.text === "" ||
				    mountCombo.currentText === "" ||
				    userField.text === "" ||
				    passwordField.text === "") {
				    showMessage("Fill all fields")
				    return
				}

				var auth = userField.text + ":" + passwordField.text
				showMessage("Connecting...")

				ntripClient.connectToMountPoint(
				    hostField.text,
				    parseInt(portField.text),
				    mountCombo.currentText,
				    auth
				)
			    }
			}

			Button {
			    text: "Disconnect"
			    Layout.fillWidth: true
			    Layout.preferredHeight: 50
			    background: Rectangle { color: "#f44336"; radius: 10 }

			    onClicked: {
				ntripClient.disconnectClient()
				showMessage("Disconnected")
			    }
			}

			// Bottom Spacer
			Item { Layout.fillHeight: true }
		    }
		}
        }
    }

    // ================= DATA PAGE =================
    Item {
        id: dataPage
        anchors.fill: parent
        visible: isConnected

        property bool isDisconnected: false

        RowLayout {
            anchors.fill: parent
            anchors.margins: 15
            spacing: 15

            // ===== DATA DISPLAY =====
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 12
                color: "white"

                Column {
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 10

                    Text {
                        text: "Live GNSS Data"
                        font.pixelSize: 20
                        font.bold: true
                    }

                    Text {
                        id: dataText
                        width: parent.width
                        text: "Waiting..."
                        wrapMode: Text.Wrap
                        font.pixelSize: 16
                    }
                }
            }

            // ===== SIDE ACTIONS =====
	Rectangle {
	    Layout.preferredWidth: 250
	    Layout.fillHeight: false
	    Layout.alignment: Qt.AlignVCenter
	    height: 300
	    radius: 12
	    color: "#DDECFF"

	    ColumnLayout {
		anchors.fill: parent
		anchors.margins: 20

		// Top Spacer
		Item { Layout.fillHeight: true }

		Button {
		    text: "Disconnect"
		    Layout.fillWidth: true
		    Layout.preferredHeight: 50
		    background: Rectangle { color: "#f44336"; radius: 10 }

		    onClicked: {
		        if (!dataPage.isDisconnected) {
		            ntripClient.disconnectClient()
		            dataPage.isDisconnected = true
		            dataText.text = "Disconnected"
		        }
		    }
		}

		Button {
		    text: "Back"
		    Layout.fillWidth: true
		    Layout.preferredHeight: 50
		    background: Rectangle { color: "#9E9E9E"; radius: 10 }

		    onClicked: {
		        if (!dataPage.isDisconnected) {
		            ntripClient.disconnectClient()
		            dataPage.isDisconnected = true
		        }
		        isConnected = false
		    }
		}

		// Bottom Spacer
		Item { Layout.fillHeight: true }
	    }
	}
        }
    }


    // ================= BACKEND =================
    Connections {
        target: ntripClient

        function onMountPointsReceived(list) {
            mountCombo.model = list
            showMessage(list.length > 0 ? "Mountpoints loaded" : "No mountpoints")
        }

        function onConnectionStatus(s) {
            showMessage(s)

            if (s === "Connected") {
                isConnected = true
                dataPage.isDisconnected = false
                dataText.text = "Waiting..."
            }

            if (s === "Disconnected") {
                isConnected = false
            }
        }

        function onDataUpdated(line) {
            if (isConnected && !dataPage.isDisconnected)
                dataText.text = line
        }
    }
}
