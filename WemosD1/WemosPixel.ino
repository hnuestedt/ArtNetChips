#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Arduino.h>
#include <FastLED.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

#define bytes_to_short(h,l) ( ((h << 8) & 0xff00) | (l & 0x00FF) );


// Initial network settings
byte mymac[]    = {0x01,0x23,0x45,0x67,0x89,0x0F};
IPAddress myip  = { 0, 0, 0, 0 };      // If first octett is 0 setup will try to optaion DHCP-IP
IPAddress nm    = { 255, 0, 0,   0 };  

// LED-Output
const short numLeds_A   = 170; // Change if your setup has more or less LED's
	  short universe_A  = 0;   //
#define     DATA_PIN_A    12    //The data pin that the WS2812 strips are connected to.
CRGB leds_A[numLeds_A];

// LED-Output
const short numLeds_B   = 170; // Change if your setup has more or less LED's
	  short universe_B  = 1;   //
#define     DATA_PIN_B    13    //The data pin that the WS2812 strips are connected to.
CRGB leds_B[numLeds_B];

WiFiUDP Udp;
byte data[530];

//Art-Net-Definitions
const short art_net_header_size =  14;		// Minimum Header-Size (ArtPoll)
const short max_packet_size     = 530; 		// Art-DMX-Header + 512 DMX-Values
char    ArtNetHead[8]           = "Art-Net";  // first byte of an ARTDMX packet contains "Art-Net"
short   Opcode				    = 0;			// Just reserved for artnetPacket
char	shortname[18]			= "WemosPixel";
char	longname[64] 			= "Haukes WemosPixel (v 2.0)";
char	nodereport[64] 			= "#0001 [0030] ready";
short   artnet_net				= 0;
short   artnet_subnet			= 0;


//ArtPollReply
char artPollReply[] = {
				'A',	'r',	't',	'-',	'N',	'e',	't',	0x00,	0x00,	0x21,	0x00,	0x00,	0x00,	0x00,	0x36, 	0x19,	//0
				0x01,	0x00,	0x00,	0x00,	0x00,	0x12,	0x00,	0x00,	0xf2,	0x7f,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	//16
				0x00,	0x0e,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	//32
				0x00,	0x0e,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	//48
				0x00,	0x0e,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	//64
				0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	//80
				0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	//96
				0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	//112
				0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	//128
				0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	//144
				//																								                            NumPorts		Port-Types
				0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x02,	0x80,	0x80,	//160
				//				GoodInput						GoodOutput						SwIn							SwOut
				0x00,	0x00,	0x80,	0x80,	0x80,	0x80,	0x80,	0x80,	0x80,	0x80,	0x01,	0x02,	0x03,	0x04,	0x00,	0x01,	//176
				//				SwVideo	SwMacro	SwRemo	Spare							Mac													  IP
				0x02,	0x03,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	mymac[0],mymac[1],mymac[2],mymac[3],mymac[4],mymac[5],0x00,	//192
				//
				0x00,	0x00,	0x00,	0x01,	0x08,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00, 	//208
				//
				0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00					//224
			};

/*
Do some checks to see if it's an ArtNet packet. First 17 bytes are the ArtNet header, the rest is DMX values
Bytes 1  to   8 of an Art-Net Packet contain "Art-Net" so check for "Art-Net" in the first 8 bytes
Bytes 8  and  9 contain the OpCode - a 16 bit number that tells if its ArtPoll or ArtDMX
Bytes 10 and 11 contain the the Art-Net-Version (10=Major Version, 11=Minor Version)
Bytes 12 and 13 contain the sequence number of the package. This might be used to reorder the received packages (usually only needed in WAN / Wifi)
Bytes 14 and 15 contain the universe-id
*/
// callback that deals with udp/6454 received packets
void myArtnetPacket(int messagelength) {   //messagein prob dont need to use a pointer here since we aren't writing to it

	if(messagelength >= art_net_header_size && messagelength <= max_packet_size) //check size to avoid unneeded checks
	{

		//read header
		boolean match_artnet = 1;
		for (int i=0;i<7;i++)
		{
			if(data[i] != ArtNetHead[i])   //tests first byte for  "Art-Net"
			{
				match_artnet=0; break;      //if not an artnet packet we stop reading
			}
		}
		if (match_artnet==1)//continue if it matches
		{
			//check which type of packet it is: ArtPoll or ArtDMX
			Opcode=bytes_to_short(data[9],data[8]);

			if(Opcode==0x5000)//if opcode is DMX type
			{
				updatePixels();
			}
			else if(Opcode==0x2000)//if opcode is artpoll
			{
				answerPoll();
			}
			else if(Opcode==-2048)//if opcode is ArtIpProg
			{
				setIp();
			}
			else if(Opcode==0x6000)//if opcode is ArtAddress
			{
				setUniverse();
			}
		}
	}
}

int universe_number(short universe){
	return artnet_net * 256 +  artnet_subnet * 16 + universe;
}

/*
function to send the dmx data out using DmxSimple library function
Reads data directly from the packetBuffer and sends straight out
*/
void updatePixels()
{
	short universe = bytes_to_short(data[15],data[14]);
	if(universe == universe_number(universe_A)){
		for(short i = 0; i < numLeds_A; i++){
			short dmx = 3*i;
			short r = bytes_to_short(byte(0), data[dmx+18]);
			short g = bytes_to_short(byte(0), data[dmx+19]);
			short b = bytes_to_short(byte(0), data[dmx+20]);
			leds_A[i] = CRGB(r,g,b);
		}
	}
	if(universe == universe_number(universe_B)){
		for(short i = 0; i < numLeds_B; i++){
			short dmx = 3*i;
			short r = bytes_to_short(byte(0), data[dmx+18]);
			short g = bytes_to_short(byte(0), data[dmx+19]);
			short b = bytes_to_short(byte(0), data[dmx+20]);
			leds_B[i] = CRGB(r,g,b);
		}
	}
	FastLED.show();
}

void answerPoll(){
	myip = WiFi.localIP();
	//Set IP
	for(int i = 10; i <= 13; i++){
		artPollReply[i] = myip[i-10];
	}
	for(int i = 207; i <= 210; i++){
		artPollReply[i] = myip[i-207];
	}
	//Set Net & Subnet
	for(int i = 18; i <= 19; i++){
		artPollReply[i] = 0x00;
	}
	//Set Shortname
	for(int i = 26; i <= 43; i++){
		artPollReply[i] = shortname[i-26];
	}
	//Set Longname
	for(int i = 44; i <= 107; i++){
		artPollReply[i] = longname[i-44];
	}
	//Set NodeReport
	for(int i = 108; i <= 171; i++){
		artPollReply[i] = nodereport[i-108];
	}
	//Set MAC
	for(int i = 201; i <= 206; i++){
		artPollReply[i] = mymac[i-201];
	}
  Serial.println();
	artPollReply[18] = artnet_net;
	artPollReply[19] = artnet_subnet;
	artPollReply[190]= universe_A;
	artPollReply[191]= universe_B;

	delay(10);

	Udp.beginPacket(Udp.remoteIP(), 6454);
	Udp.write(artPollReply, sizeof artPollReply);
	Udp.endPacket();

}
String printIP(const IPAddress& address){
  return String() + address[0] + "." + address[1] + "." + address[2] + "." + address[3];
}
void setIp()
{
	myip = IPAddress(data[16], data[17], data[18], data[19]);

	if(data[20] > 0){
		nm = IPAddress(data[20], data[21], data[22], data[23]);
	}

	Serial.print("New IP: ");
	Serial.println(String() + data[16]+ "." +data[17]+ "." +data[18]+ "." +data[19]);

	eeprom_store_values();
	answerPoll();

  Serial.println("Restarting System");
  delay(100);
  ESP.restart();
}
void setUniverse()
{

	if((data[12] & 0b10000000)  &&  data[12] != 0x7f){
		artnet_net = data[12] & 0b01111111;
	}
	if((data[104] & 0b10000000)  &&  data[104] != 0x7f){
		artnet_subnet = data[104] & 0b01111111;
	}
	if((data[100] & 0b10000000)  &&  data[100] != 0x7f){
		universe_A = data[100] & 0b01111111;
	}
	if((data[101] & 0b10000000)  &&  data[101] != 0x7f){
		universe_B = data[101] & 0b01111111;
	}
	if(data[14] > 0){
		for(int i=14;i<32;i++){
			shortname[i-14] = data[i];
		}
	}
	if(data[32] > 0){
		for(int i=32;i<96;i++){
			longname[i-32] = data[i];
		}
	}

	Serial.print(F("Universe A is now "));
	Serial.println(universe_number(universe_A));
	Serial.print(F("Universe B is now "));
	Serial.println(universe_number(universe_B));

	eeprom_store_values();
	answerPoll();
}

void eeprom_read_values(){
	EEPROM.get(  0, myip);
	EEPROM.get( 10, nm);
	EEPROM.get( 20, artnet_net);
	EEPROM.get( 30, artnet_subnet);
	EEPROM.get( 40, universe_A);
	EEPROM.get( 42, universe_B);
	EEPROM.get( 50, shortname);
	EEPROM.get(100, longname);
}
void eeprom_store_values(){
  Serial.println("Storing values");
  Serial.print("MyIP: ");
  Serial.println(String() + myip[0]+ "." +myip[1]+ "." +myip[2]+ "." +myip[3]);
	EEPROM.put(  0, myip);
	EEPROM.put( 10, nm);
	EEPROM.put( 20, artnet_net);
	EEPROM.put( 30, artnet_subnet);
	EEPROM.put( 40, universe_A);
	EEPROM.put( 42, universe_B);
	EEPROM.put( 50, shortname);
	EEPROM.put(100, longname);
	EEPROM.commit();
}

void setup () {
	Serial.begin(115200);
	EEPROM.begin(256);

	Serial.println(F(" -- [ WemosPixel 2 ] -- "));
	Serial.println(F("Connecting to WiFi..."));
  
  //WiFiManager, Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wm;

	pinMode(4, INPUT);
  delay(100); //Needed to settle voltages for digitalRead
	if(digitalRead(4)){
    // reset settings - wipe stored credentials for testing
    // these are stored by the esp library
    Serial.println("RESETTING");
		eeprom_store_values();
    wm.resetSettings();
	}else{
    Serial.println("USE STORED VALUES");
		eeprom_read_values();
	}
  if(myip[0] > 0){
	  wm.setSTAStaticIPConfig(myip, myip, nm); // optional DNS 4th argument
  }

  bool res = wm.autoConnect(); 
  if(!res){
    Serial.println("Failed to connect");
    //ESP.restart();
  }else{
    Serial.println("connected to wifi! yay :)");
  }

	Udp.begin(6454);

  Serial.print(F("IP address: "));
  Serial.println(WiFi.localIP());
  //Store MyMac for ArtPollReply packages
  WiFi.macAddress(mymac);

  Serial.println("MAC: "+WiFi.macAddress());

	FastLED.addLeds<WS2812, DATA_PIN_A, GRB>(leds_A, numLeds_A);
	FastLED.addLeds<WS2812, DATA_PIN_B, GRB>(leds_B, numLeds_B);
}

void loop () {
	int packetSize = Udp.parsePacket();
  int i = 0;
	if (packetSize)
	{
		//Serial.printf("Received %d bytes from %s, port %d\n", packetSize, Udp.remoteIP().toString().c_str(), Udp.remotePort());
		int len = Udp.read(data, 255);
		if (len > 0)
		{
			data[len] = '\0';
		}

		myArtnetPacket(len);
	}
}
