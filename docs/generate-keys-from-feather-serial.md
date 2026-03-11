Managing unique keys across multiple boards is a massive headache. Hardcoding them in the sketch means you will inevitably flash the wrong keys to the wrong board during a future upgrade, which immediately breaks the connection.

For an Arduino M0 based project, the absolute easiest approach is to generate the LoRaWAN credentials dynamically using the microcontroller's hardware.
Generating Keys from the Silicon ID

The SAMD21 chip on your Arduino M0 has a permanent, 128-bit unique serial number burned into it at the factory. Instead of typing out arrays of hex values in your code, you can tell the firmware to read this silicon ID on boot and use it to construct your DevEUI and AppKey.

You can then define a single, shared secret "salt" in your code to mix with the serial number. This ensures your keys are secure but repeatable.

The benefits of this approach:

    You compile exactly one firmware file.

    You flash that exact same file to every single tank sensor.

    If you need to upgrade the code in two years, you just compile the new version and flash it, the board will automatically reconstruct its correct keys on boot.

The Workflow:

    You flash the universal firmware to a new board.

    You plug it into your computer and open the Serial Monitor.

    The board prints its dynamically generated DevEUI and AppKey to the screen.

    You copy those values and register the new device in your backend.

Adding the Generated Device to The Things Network (TTN)

Once your board spits out its unique keys in the serial monitor, you will need to tell the network server to accept them. Here is the step-by-step guide to registering these custom keys in the TTN Console:

    Log in to your TTN Console and select your specific Application.

    In the left-hand menu, click on End devices.

    Click the blue + Register end device button on the top right.

    Under "Input Method", click the Enter end device specifics manually tab.

    In the "Frequency plan" dropdown, select Europe 863-870 MHz (SF9 for RX2 - recommended).

    In the "LoRaWAN version" dropdown, select MAC V1.0.3 (this is the standard for the LMIC library you are using).

    Under "Provisioning information", paste the JoinEUI (AppEUI) that you decided to use for all your devices.

    Click Confirm.

    Paste the unique DevEUI and AppKey that your Arduino printed to the serial monitor into their respective fields.

    Click the Register end device button at the bottom.

Would you like me to write the C++ function that reads the SAMD21 hardware registers and formats them into the LMIC os_getDevEui and os_getDevKey buffers?