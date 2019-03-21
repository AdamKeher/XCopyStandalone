// 01. AT               Attention
// 02. AT+RST           Soft reset
// 03. AT+GMR           Retrieve firmware version
// 04. AT+CWMODE        Operation mode selection, 1 = sta, 2 = AP, 3 = both
// 05. AT+CWJAP         Join network
// 06. AT+CWLAP         Available network listing
// 07. AT+CWQAP         Exit network
// 08. AT+CWSAP         Setup network name, password, radio channel and security scheme
// 09. AT+CWLIF         Listing connected stations, only AP mode
// 10. AT+CIPSTATUS     Connection listing
// 11. AT+CIPSTART      Initiating connection
// 12. AT+CIPSEND       Sending data
// 13. AT+CIPCLOSE      Closing connection
// 14. AT+CIFSR         Displaying IP from access point
// 15. AT+CIPMUX        Single or multiple connection selection, 0 = single, 1 = multiple
// 16. AT+CIPSERVER     Socket server on/off,  AT+CIPSERVER=<mode>[,<port>,] mode 0 close, mode 1 up <port #>
// 17. AT+CIPMODE       Serial port transparent or connecdtion based data output selection
// 18. AT+CIPSTO        Socket server automatic connection timeout setting
// 19. AT+IPD             Serial port connection based data output
// 20. AT+CIOBAUD       ? gives baud rate setting, =9600 (e.g.)sets a rate
// 21. ATE#             controls echoing of received commands, #=0 disable, #=1 enable echo

#ifndef XCOPYESP8266_H
#define XCOPYESP8266_H

#include <Arduino.h>
#include <Streaming.h>

class XCopyESP8266
{
public:
  XCopyESP8266(HardwareSerial serial, uint32_t baudrate);
  bool begin();
  bool connect(String ssid, String password, uint32_t timeout);
  String sendCommand(String command, bool strip = false, uint32_t timeout = 250);
  String Version();
  void setEcho(bool status);

private:
  char OK_EOC[5] = "OK\r\n";
  char ER_EOC[5] = "ER\r\n";
  HardwareSerial _serial;
};

#endif // XCOPYESP8266_H