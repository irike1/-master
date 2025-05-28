This project is a WIP. I am trying to setup a nrf5340 and have had many issues with the official samples. 
These are the short-term goals of my project
1. setup a functioning usb keyboard program (complete)
2. setup zephyr's new kscan i.e. keyboard scan matrix (complete)
3. Control a ws2812b led chain (complete)
4. control a ws2805 chain
5. setup Bluetooth connection

Since (as far as I know) zephyr does not 32-bit rgb chains, I am currently attempting to stop using their led_strip drivers, and instead, reference a modified, local version. While I would love to help update the official drivers, I am not confident enough to not break everything.