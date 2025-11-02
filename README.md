# flipperzero-i2ctools

Set of i2c tools for Flipper Zero

![Preview](i2ctools.gif)

## Dependencies

Crypto features rely on Microchip's [CryptoAuthLib](https://github.com/MicrochipTech/cryptoauthlib).
Add the library as a git submodule before building firmware with crypto
support:

```
git submodule add https://github.com/MicrochipTech/cryptoauthlib lib/cryptoauthlib
git submodule update --init --recursive
```

The build will continue to work without the submodule present, but crypto
capabilities will be reported as unavailable on the device.

## Wiring

C0 -> SCL

C1 -> SDA

GND -> GND

>/!\ Target must use 3v3 logic levels. If you not sure use an i2c isolator like ISO1541

## Tools

### Scanner

Look for i2c peripherals adresses

### Sniffer

Spy i2c traffic

### Sender

Send command to i2c peripherals and read result 

## TODO
- [ ] Kicad module
- [ ] Improve UI
- [ ] Refactor Event Management Code
- [ ] Add Documentation

## V2
- [ ] Read more than 2 bytes in sender mode
- [ ] Add 10-bits adresses support
- [ ] Test with rate > 100khz
- [ ] Save records (Sigrok compatible?)
- [ ] Play from files
- [ ] Remove max data size
- [ ] Remove max frames read size