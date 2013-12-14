/*
 * homeserver.cpp
 *
 * Created: 23-10-2013 18:21:41
 *  Author: LECAS
 */ 
#include "Arduino.h"
#include "SD/SD.h"
#include "ethernet/Ethernet.h"
#include "RF24/nRF24L01.h"
#include "RF24/RF24.h"
#include "SPI/SPI.h"
#include "printf.h"
#include "avr/wdt.h"
#include "avr/eeprom.h"

#define sirene				23
#define activeLED			48
#define netLED				47
#define wifiLED				46
#define ADDRconfig			0x01
#define SERVERpipe			0x02
#define configMODE			0x03
#define pingpong			0x04
#define searchMODE			0x05
#define set_server_route	0x06
#define configured			0xDE
#define add_address			0x08
#define scan_zone			0x09
#define not_found			0x10
#define destroy_net			0x11
#define switch_event		0x12
#define all_on				0x13
#define all_off				0x14
#define output_toogle		0x15
#define set_active			0x16

struct {
	uint64_t destino;
	byte comando;
	byte dados[8];
	}RFbuffer;

struct dev{
	uint64_t addr;
	char name[7];
	};
struct interruptor{
	uint64_t addr;
	uint8_t index;
	};

struct p_luz{
	uint64_t addr;
	uint8_t index;
	uint8_t estado;
	};
	
struct circuito{
	char nome[10];
	p_luz luz;
	interruptor botao;
	};

/*Ethernet stuff*/
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
byte ip[] = { 192, 168, 9, 15 };
Server server(80);

/*RF24 wifi stuff*/
uint64_t my_pipe=0x00000000FE;
uint64_t EEMEM my_pipe_addr;
RF24 radio(8,9);


/*SD stuff*/
File file;

/*hardware stuff*/
uint8_t led_state=LOW;

void blink_led(){
	digitalWrite(activeLED,HIGH);
	delayMicroseconds(5);
	digitalWrite(activeLED,LOW);
	return;
}
void get_user_input(char *buffer,size_t tamanho){
	blink_led();
	size_t tam;
	while(!Serial.available());
	if (tamanho>1){
		tam=Serial.readBytes(buffer,tamanho);
		*(buffer+tam)='\0';
	}else{
		tam=1;
		*buffer=Serial.read();
	}
	for(int i=0;i<tam;i++)
		printf("%c",*(buffer+i));
	printf("\r\n");
	return;
}
boolean load_from_file(char *nome,uint8_t *what,size_t tamanho,uint8_t number){
	blink_led();
	uint32_t pos;
	pos=number*tamanho;
	file=SD.open(nome,FILE_READ);
	if (file){
		if (file.seek(pos)==false || pos==file.size()){
			return false;
		}
		for (int i=0;i<tamanho;i++){
			*(what+i)=file.read();
		}
		file.close();
		return true;
	}else{
		printf("file error\r\n");
		return false;
	}
}
boolean save_to_file(char *nome,uint8_t *what,size_t tamanho,uint8_t onde){
	blink_led();
	uint32_t pos;
	pos=onde*tamanho;
	file=SD.open(nome,FILE_WRITE);
	if (file){
		if (file.seek(pos)==false || pos==file.size()){
			return false;
		}
		file.write(what,tamanho);
		file.close();
		return true;
	}else{
		printf("file error\r\n");
		return false;
	}
}
void create_circuit(){
	circuito cir;
	dev device;
	char dev_name[7];
	uint8_t index;
	uint8_t i=0;
	boolean found=false;
	printf("new circuit name? ");
	get_user_input(cir.nome,sizeof(cir.nome));
	printf("input device name? ");
	get_user_input(dev_name,sizeof(dev_name));
	while(load_from_file("devs.lst",(uint8_t *)&device,sizeof(device),i)){
		blink_led();
		if (strcmp(device.name,dev_name)==0){
			found=true;
			break;
		}
		i++;
	}
	if (found){
		cir.botao.addr=device.addr;
		printf("input index? ");
		get_user_input((char *)&index,1);
		index-='0';
		cir.botao.index=index;
	}else{
		printf("name not found\r\n");
		return;
	}
	i=0;
	found=false;
	printf("output device name? ");
	get_user_input(dev_name,sizeof(dev_name));
	while(load_from_file("devs.lst",(uint8_t *)&device,sizeof(device),i)){
		blink_led();
		if (strcmp(device.name,dev_name)==0){
			found=true;
			break;
		}
		i++;
	}
	if (found){
		cir.luz.addr=device.addr;
		printf("output index? ");
		get_user_input((char *)&index,1);
		index-='0';
		cir.luz.index=index;
		cir.luz.estado=LOW;
	}else{
		printf("name not found\r\n");
		return;
	}
	printf("save?(y/n)\r\n");
	get_user_input((char *)&index,1);
	if (index=='y'){
		file=SD.open("cirs.lst",FILE_WRITE);
		if (file){
			file.write((byte *)&cir,sizeof(cir));
		}
		file.close();
		printf("saved\r\n");
	}else{
		return;
	}
}
void save_new_address(uint64_t *addr){
	blink_led();
	printf("addr:%lX%08lX added to addr.lst file.\r\n",(uint32_t)(*addr>>32),(uint32_t)*addr);
	file=SD.open("addr.lst",FILE_WRITE);
	if (file){
		file.write((byte *)addr,8);
	}
	file.close();
	return;
}
boolean load_address(uint64_t *addr,uint8_t number){
	blink_led();
	uint32_t position;
	boolean sucess;
	*addr=0;
	position=((number)*8);
	file=SD.open("addr.lst",FILE_READ);
	if (file){
		sucess=file.seek(position);
		if (sucess==false || position==file.size()){
			return false;
		}
		uint8_t buffer[8]={NULL};
		for(int i=0;i<8;i++){
			buffer[i]=(uint8_t)file.read();
		}
		for (int i=7;i>=0;i--){
			*addr|=buffer[i];
			if (i!=0)
			*addr=*addr<<8;
		}
	}else{
		printf("file error\r\n");
		return false;
	}
	file.close();
	return sucess;
}
void add_names_to_devices(){
	uint8_t i=0;
	uint64_t mask=0xFFFFFFFFFF000000LL;
	dev device;
	uint8_t tam;
	printf("Please add names to the devices\r\n");
	while(load_address(&RFbuffer.destino,i)){
		blink_led();
		device.addr=RFbuffer.destino;
		RFbuffer.comando=all_on;
		radio.stopListening();
		radio.openWritingPipe((RFbuffer.destino & mask));
		radio.write(&RFbuffer,sizeof(RFbuffer));
		radio.startListening();
		printf("enter name for device:%d,with addr:%lX%08lX\r\n",i+1,(uint32_t)(device.addr>>32),(uint32_t)device.addr);
		while(!Serial.available());
		do {
			blink_led();
			tam=Serial.readBytes(device.name,sizeof(device));
			if (tam>=sizeof(device.name)){
				printf("name must be smaller than 7 letter\r\n");
			}else{
				device.name[tam]='\0';
				printf("device name set to:");
				int n=0;
				do{
					printf("%c",device.name[n]);
					n++;
				} while (device.name[n]!='\0');
				printf("\r\n");
				file=SD.open("devs.lst",FILE_WRITE);
				if (file){
					file.write((byte *)&device,sizeof(device));
				}
				file.close();
			}
		} while (tam>=sizeof(device.name));
		RFbuffer.comando=all_off;
		radio.stopListening();
		radio.openWritingPipe((RFbuffer.destino & mask));
		radio.write(&RFbuffer,sizeof(RFbuffer));
		radio.startListening();
		i++;
	}
	printf("done\r\n");
	return;
}
void wifinetworkconfig(){
	uint64_t addr=1;
	uint64_t mask=0xFFFFFFFFFF000000LL;
	uint64_t next_addr;
	boolean ok;
	boolean addr_load;
	uint8_t last_dev_count=NULL;
	uint8_t dev_count=NULL;
	uint8_t total_devs=NULL;
	uint8_t zone=1;
	uint8_t	addr_count=NULL;
	
	my_pipe=(uint8_t)random(1,0xFF);
	my_pipe=(my_pipe<<32);
	eeprom_write_block(&my_pipe,&my_pipe_addr,sizeof(my_pipe));
	radio.openReadingPipe(1,my_pipe);
	printf("Server address set to:%lX%08lX\r\n",(uint32_t)(my_pipe>>32),(uint32_t)my_pipe);
	
	do{		
		printf("starting configuration of zone:%d\r\n",zone);
		delay(1000);
		do{
			dev_count=0;
			if (zone>1)	{
				addr_load=load_from_file("addr.lst",(uint8_t *)&next_addr,sizeof(next_addr),addr_count);
				printf("extending with addr:%lX%08lX\r\n",(uint32_t)(next_addr>>32),(uint32_t)next_addr);
				addr_count++;
			}
			addr=1;
			while(addr<0xFFLL){
				blink_led();
				if (zone>1){
					RFbuffer.destino=next_addr;
					RFbuffer.dados[1]=addr;
					RFbuffer.dados[0]=zone;
					RFbuffer.comando=scan_zone;
					radio.stopListening();
					radio.openWritingPipe((next_addr & mask));
					ok=radio.write(&RFbuffer,sizeof(RFbuffer));
					radio.startListening();
					if(ok){
						while(!radio.available());
						radio.read(&RFbuffer,sizeof(RFbuffer));
						if (RFbuffer.comando!=not_found){
							save_new_address(&RFbuffer.destino);
							dev_count++;
						}
					}
					addr++;
				}
				if(zone==1){
					RFbuffer.destino=my_pipe;
					RFbuffer.dados[0]=zone;
					radio.stopListening();
					radio.openWritingPipe(addr);
					ok=radio.write(&RFbuffer,sizeof(RFbuffer));
					radio.startListening();
					if (ok){
						while(!radio.available());
						radio.read(&RFbuffer,sizeof(RFbuffer));
						save_new_address(&RFbuffer.destino);
						dev_count++;
					}
					addr++;
				}
				delay(20);
			}
			if (last_dev_count!=0)
				last_dev_count--;
		}while(last_dev_count>0);
		last_dev_count=dev_count;
		total_devs+=dev_count;
		zone++;
	}while(zone<4 && last_dev_count>0);
	printf("Created %d zone(s) with a total of %d device(s).\r\n",(zone-2),total_devs);
	if (total_devs>0){
		add_names_to_devices();
	}
	return;
}
void destroy_network(){
	uint32_t addr_n;
	uint64_t mask=0xFFFFFFFFFF000000LL;
	uint32_t timeout;
	if (SD.exists("addr.lst")){
		file=SD.open("addr.lst",FILE_READ);
		if (file){
			addr_n=file.size();
		}else{
			printf("file error\r\n");
		}
		file.close();
		addr_n/=8;
		while(addr_n>0){
			blink_led();
			addr_n--;
			load_from_file("addr.lst",(uint8_t*)&RFbuffer.destino,sizeof(RFbuffer.destino),addr_n);
			RFbuffer.comando=destroy_net;
			radio.stopListening();
			radio.openWritingPipe((RFbuffer.destino & mask));
			timeout=millis();
			while(!radio.write(&RFbuffer,sizeof(RFbuffer)) && millis()-timeout<500);
			radio.startListening();
			delay(50);
		}
	}
	if (SD.exists("addr.lst")){
		SD.remove("addr.lst");
	}
	if (SD.exists("devs.lst")){
		SD.remove("devs.lst");
	}
	if (SD.exists("cirs.lst")){
		SD.remove("cirs.lst");
	}
	printf("wifi network and circuits destroyed\r\n");
	return;
}
void compile_xml(){
	blink_led();
	circuito circ;
	uint8_t i=0;
	if (SD.exists("lights.xml")){
		SD.remove("lights.xml");
	}
	file=SD.open("lights.xml",FILE_WRITE);
	if (file){
		file.println("<lights>");
	}else{
		printf("file error\r\n");
	}
	file.close();		
	while(load_from_file("cirs.lst",(uint8_t *)&circ,sizeof(circ),i)){
		file=SD.open("lights.xml",FILE_WRITE);
		if (file){
			file.println("<div>");
			file.print("<nome>");
			file.print(circ.nome);
			file.println("</nome>");
			file.print("<estado>");
			if (circ.luz.estado==1){
				file.print("on");
			}else{
				file.print("off");
			}
			file.println("</estado>");
			file.println("</div>");
		}else{
			printf("file error\r\n");
		}
		file.close();
		i++;
	}
	file=SD.open("lights.xml",FILE_WRITE);
	if (file){
		file.println("</lights>");
	}else{
		printf("file error\r\n");
	}
	file.close();
	return;	
}
void switch_event_handler(uint64_t *addr,uint8_t index){
	interruptor botao;
	circuito cir;
	uint8_t i=0;
	uint64_t mask=0xFFFFFFFFFF000000LL;
	botao.addr=*addr;
	botao.index=index;
	while(load_from_file("cirs.lst",(uint8_t *)&cir,sizeof(cir),i)){
		blink_led();
		if ((cir.botao.addr==botao.addr)&&(cir.botao.index==botao.index)){
			RFbuffer.comando=output_toogle;
			RFbuffer.destino=cir.luz.addr;
			RFbuffer.dados[0]=cir.luz.index;
			RFbuffer.dados[1]=(!cir.luz.estado);
			radio.stopListening();
			radio.openWritingPipe((RFbuffer.destino & mask));
			radio.write(&RFbuffer,sizeof(RFbuffer));
			radio.startListening();
			cir.luz.estado=(!cir.luz.estado);
			save_to_file("cirs.lst",(uint8_t *)&cir,sizeof(cir),i);
			break;
		}
		i++;
	}
	printf("%10s %d\r\n",cir.nome,cir.luz.estado);
	compile_xml();
	return;
	
}
void toogle_output(circuito *cir,uint8_t i){
	blink_led();
	uint64_t mask=0xFFFFFFFFFF000000LL;
	RFbuffer.comando=output_toogle;
	RFbuffer.destino=cir->luz.addr;
	RFbuffer.dados[0]=cir->luz.index;
	RFbuffer.dados[1]=(!cir->luz.estado);
	radio.stopListening();
	radio.openWritingPipe((RFbuffer.destino & mask));
	radio.write(&RFbuffer,sizeof(RFbuffer));
	radio.startListening();
	cir->luz.estado=(!cir->luz.estado);
	save_to_file("cirs.lst",(uint8_t *)cir,sizeof(circuito),i);
	compile_xml();
	return;
}
void eth_command_handler(char *httprequest,uint8_t i){

	uint8_t n=0;
	char nome[10];
	circuito cir;
	if (*(httprequest+(i++))=='?'){
		while(*(httprequest+i)!=' '){
			nome[n]=*(httprequest+i);
			i++;
			n++;
		}
		nome[n]='\0';
		i=0;
		while(load_from_file("cirs.lst",(uint8_t *)&cir,sizeof(cir),i)){
			if (strcmp(cir.nome,nome)==0){
				toogle_output(&cir,i);
				break;
			}
			i++;
		}
	}
	return;
}
void serial_handler(){

	char buffer[15];
	if (Serial.available()){
		blink_led();
		int len=Serial.readBytes(buffer,sizeof(buffer));
		buffer[len]='\0';
		if (strcmp(buffer,"net config")==0){
			wifinetworkconfig();
		}
		if (strcmp(buffer,"del net")==0){
			destroy_network();
		}
		if (strcmp(buffer,"mk cir")==0){
			create_circuit();
		}		
		if (strcmp(buffer,"siren on")==0){
			digitalWrite(sirene,HIGH);
		}
		if (strcmp(buffer,"siren off")==0){
			digitalWrite(sirene,LOW);
		}
		
	}
	return;

}
void RF24_handler(){

	if (radio.available()){
		blink_led();
		radio.read(&RFbuffer,sizeof(RFbuffer));
		switch (RFbuffer.comando){
			case switch_event:
				switch_event_handler(&RFbuffer.destino,RFbuffer.dados[0]);
				break;
		}
	}
	return;
}
void network(void){
	
	Client client = server.available();
	if (client) {
		blink_led();
		boolean command=false;
		char request[20];
		int i = 0;
		boolean current_line_is_blank = true;
		request[19] = '\0';
		while (client.connected()) {
			blink_led();
			if (client.available()) {
				char c = client.read();
				if (i < 19) {
					request[i] = c;
					i++;
				}
				if (c == '\n' && current_line_is_blank) {
					if(strncmp(request,"GET /webpage.htm",14)==0){
						file=SD.open("webpage.htm",FILE_READ);
					}
					if (strncmp(request,"GET /control.htm",16)==0){
						file=SD.open("control.htm",FILE_READ);
					}
					if (strncmp(request,"GET /lights.xml",15)==0){
						file=SD.open("lights.xml",FILE_READ);
					}
					if (strncmp(request,"GET /?",6)==0){
						command=true;
						client.print("ok");
					}
					if (file){
						while(file.available()){
							client.write(file.read());
						}
						file.close();
					}		
					break;
				}
				if (c == '\n') {
					current_line_is_blank = true;
				} else if (c != '\r') {
					current_line_is_blank = false;
				}
			}
		}
		client.stop();
		if (command){
			eth_command_handler(request,5);
		}
	}
	return;
}
void setup(){

	Serial.begin(115200);
	printf_begin();
	pinMode(activeLED,OUTPUT);
	pinMode(sirene,OUTPUT);
	pinMode(netLED,OUTPUT);
	pinMode(wifiLED,OUTPUT);
	if (!SD.begin(13)){
		printf("SD error\r\n");
	}
	Ethernet.begin(mac,ip);
	server.begin();
	eeprom_read_block(&my_pipe,&my_pipe_addr,sizeof(my_pipe_addr));
	radio.begin();
	radio.setRetries(20,40);
	radio.setPayloadSize(sizeof(RFbuffer));
	radio.openReadingPipe(1,my_pipe);
	radio.setPALevel(RF24_PA_MAX);
	radio.startListening();
	/*attachInterrupt(5,network,RISING);*/
	randomSeed(analogRead(15));
	printf("system start...%d bytes of free ram\r\n",FreeRam());
	
}
void loop(){
	blink_led();
	serial_handler();
	network();
	RF24_handler();

}