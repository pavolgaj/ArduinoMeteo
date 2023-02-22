#include "SFE_BMP180.h"
#include <Wire.h>
#include <dht.h>
#include <LiquidCrystal_I2C.h>
#include <SD.h>

//#define logSerial 0
//#define logBar 0
//#define logTemp 0

#define rebootSD 0

#define dhtPin 2

#define chipSelect 10

SFE_BMP180 pressure;

#define ALTITUDE 280.0 //PO-J3

//LiquidCrystal_I2C lcd(0x27,16,2);
LiquidCrystal_I2C lcd(0x27,20,4);

#define DS3231_I2C_ADDRESS 0x68

boolean output=true;  //zapis?
byte counter=200;     //pocitadlo pre meranie

//pre priemery na zapis
byte n=0;
double Pv[3]={0,9999,0};  //suma,min,max
float tv[3]={0,100,-100}; //suma,min,max
float hv[3]={0,100,0};  //suma,min,max

//denne extremy
double Pd[2]={9999,0};  //min,max
float td[2]={100,-100}; //min,max
float hd[2]={100,0};  //min,max
byte oldDay=0;

byte P_trend=5;  // trend tlaku: 0-klesa, 1-rovnaky, 2-stupa, 5-nic
double P_vals[12];
byte t_trend=5;  // trend teploty: 0-klesa, 1-rovnaky, 2-stupa, 5-nic
double t_vals[12];
byte h_trend=5;  // trend vlhkosti: 0-klesa, 1-rovnaky, 2-stupa, 5-nic
double h_vals[12];
byte i=0;

boolean err=false;

byte bcdToDec(byte val){
    return( (val/16*10) + (val%16) );
}

byte decToBcd(byte val){
    return( (val/10*16) + (val%10) );
}

#ifdef rebootSD
void reboot() {
  asm volatile ("  jmp 0");
}
#endif

void readDS3231time(byte *second, byte *minute, byte *hour, byte *dayOfWeek, byte *dayOfMonth, byte *month, byte *year){
    Wire.beginTransmission(DS3231_I2C_ADDRESS);
    Wire.write(0); // set DS3231 register pointer to 00h
    Wire.endTransmission();
    Wire.requestFrom(DS3231_I2C_ADDRESS, 7);

    *second = bcdToDec(Wire.read() & 0x7f);
    *minute = bcdToDec(Wire.read());
    *hour = bcdToDec(Wire.read() & 0x3f);
    *dayOfWeek = bcdToDec(Wire.read());      // Sun=1
    *dayOfMonth = bcdToDec(Wire.read());
    *month = bcdToDec(Wire.read());
    *year = bcdToDec(Wire.read());   
}

void setDS3231time(byte second, byte minute, byte hour, byte dayOfWeek, byte dayOfMonth, byte month, byte year){
    // sets time and date data to DS3231
    Wire.beginTransmission(DS3231_I2C_ADDRESS);
    Wire.write(0); // set next input to start at the seconds register
    Wire.write(decToBcd(second)); // set seconds
    Wire.write(decToBcd(minute)); // set minutes
    Wire.write(decToBcd(hour)); // set hours
    Wire.write(decToBcd(dayOfWeek)); // set day of week (1=Sunday, 7=Saturday)
    Wire.write(decToBcd(dayOfMonth)); // set date (1 to 31)
    Wire.write(decToBcd(month)); // set month
    Wire.write(decToBcd(year)); // set year (0 to 99)
    Wire.endTransmission();
}

double date2jd(int year, byte mon, byte day, byte hour, byte minute, byte second){
    //convert date and time to julian date
    year+=2000;
    if (mon<=2) {
        year-=1;
        mon+=12;
    }         
    int a=int(year/100);
    int b=2-a+int(a/4);
    double jd=double((unsigned long)(365.25*(year+4716))+int(30.6001*(mon+1))+day)+double(b)-1524.5-2450000;
    jd+=+double(hour)/24.0+double(minute)/1440.0+double(second)/86400.0;

    #ifdef logSerial        
        Serial.println(double((unsigned long)(365.25*(year+4716))+int(30.6001*(mon+1))+day));
        Serial.println(double(hour)/24.0+double(minute)/1440.0+double(second)/86400.0+double(b)-1524.5);
        Serial.println(double((unsigned long)(365.25*(year+4716))+int(30.6001*(mon+1))+day)+double(hour)/24.0+double(minute)/1440.0+double(second)/86400.0+double(b)-1524.5);
        Serial.println(jd);
    #endif
    
    return jd;
}

void zobraz(double t,double P, double h, byte year, byte mon, byte day, byte dayW, byte hour, byte minute, byte second, byte typ){
  //lcd.clear();
  lcd.setCursor(10,0);
  lcd.print(F(" "));

  byte offset=2;    //posun oproti UTC
  byte mon1=mon;

  double jd=date2jd(year,mon,day,hour,minute,second);
  
  //zmena casu
  if (((mon==3) or (mon==10)) and (day>24)) {
    //double jd=date2jd(year,mon,day,hour,minute,second)+2450000;
    //byte x=6-(int(jd+0.5)%7);  //?
    byte x=7-dayW;
    //if (x==0) { x=7; }
    if (day+x>=31) { mon1+=1; }
  }
  if ((mon1<4) or (mon1)>10) { offset=1; }
  
  hour+=offset;
  if (hour>=24) {
    hour-=24;
    day+=1;
    if (((mon==1) or (mon==3) or (mon==5) or (mon==7) or (mon==8) or (mon==10)) and (day==32)){
      day=1;
      mon+=1;
    }
    else if (((mon==4) or (mon==6) or (mon==9) or (mon==11)) and (day==31)){
      day=1;
      mon+=1;
    }
    else if ((mon==12) and (day==32)){
      day=1;
      mon=1;
      year+=1;
    }
    else if ((mon==2) and (day==29) and (year%4>0)){
      day=1;
      mon+=1;
    }
    else if ((mon==2) and (day==30) and (year%4==0)){
      day=1;
      mon+=1;
    }
  }
  
  lcd.setCursor(0,0);
  if (day<10) { lcd.print(F("0")); lcd.setCursor(1,0); }
  lcd.print(day);
  lcd.setCursor(2,0);
  lcd.print(F("."));
  lcd.setCursor(3,0);
  if (mon<10) { lcd.print(F("0")); lcd.setCursor(4,0); }
  lcd.print(mon);
  lcd.setCursor(5,0);
  lcd.print(F("."));
  lcd.setCursor(6,0);
  lcd.print(F("20"));
  lcd.setCursor(8,0);
  lcd.print(year);
  
  lcd.setCursor(12,0);
  if (hour<10) { lcd.print(F("0")); lcd.setCursor(13,0); }
  lcd.print(hour);
  lcd.setCursor(14,0);
  lcd.print(F(":"));
  lcd.setCursor(15,0);
  if (minute<10) { lcd.print(F("0")); lcd.setCursor(16,0); }
  lcd.print(minute);
  lcd.setCursor(17,0);
  lcd.print(F(":"));
  lcd.setCursor(18,0);
  if (second<10) { lcd.print(F("0")); lcd.setCursor(19,0); }
  lcd.print(second);

  lcd.setCursor(0,1);
  lcd.print(F("Teplota:"));
  lcd.setCursor(11,1);
  //lcd.setCursor(12,1);
  //if (t<-10) { lcd.setCursor(11,1); }
  //if ((t<10) and (t>=0)) { lcd.setCursor(13,1); }
  if (typ==1) { lcd.write(3); }   //min
  else if (typ==2) { lcd.write(4); }   //max
  else if (t_trend<3) { lcd.write(t_trend); }   //trend
  else {lcd.print(F(" "));}
  lcd.setCursor(12,1);
  lcd.print(F(" "));  
  lcd.setCursor(13,1);
  if (t<-10) { lcd.setCursor(12,1); }
  if ((t<10) and (t>=0)) { lcd.print(F(" "));  lcd.setCursor(14,1); }
  lcd.print(t,1);
  lcd.setCursor(17,1);
  lcd.print(F(" "));
  lcd.setCursor(18,1);
  lcd.write(6);
  lcd.setCursor(19,1);
  lcd.print(F("C"));
  
  lcd.setCursor(0,2);
  lcd.print(F("Vlhkost:"));
  lcd.setCursor(11,2);
  if (typ==1) { lcd.write(3); }   //min
  else if (typ==2) { lcd.write(4); }   //max
  else if (h_trend<3) { lcd.write(h_trend); }   //trend
  else {lcd.print(F(" "));}
  lcd.setCursor(13,2);
  lcd.print(h,1);
  lcd.setCursor(17,2);
  lcd.print(F(" "));
  lcd.setCursor(19,2);
  lcd.print(F("%"));  

  lcd.setCursor(0,3);
  lcd.print(F("Tlak:"));
  lcd.setCursor(8,3);
  if (typ==1) { lcd.write(3); }   //min
  else if (typ==2) { lcd.write(4); }   //max
  else if (P_trend<3) { lcd.write(P_trend); }   //trend
  else {lcd.print(F(" "));}
  lcd.setCursor(10,3);
  if (P<1000) { lcd.print(F(" ")); lcd.setCursor(11,3); }
  lcd.print(P,1);
  lcd.setCursor(16,3);
  lcd.print(F(" hPa"));

  if (err) {
    lcd.setCursor(10,0);
    lcd.print(F("!"));
  }
}


void zapis(byte year, byte mon, byte day, byte hour, byte minute, byte second){
  if (day==0) { return; }

  File file;

  char filename[]="20YYMM.dat";
  sprintf(filename,"20%02d%02d.dat",year,mon);

  if (! SD.exists(filename)) 
    {
      // only open a new file if it doesn't exist
      file = SD.open(filename, FILE_WRITE); 

      #ifdef logSerial
        if (file) { Serial.print(F("created new ")); Serial.println(filename); }
        else { Serial.print(F("error opening ")); Serial.println(filename); } 
      #endif
      
      file.print(F("# date(Y-M-D)"));
      file.print(F(" "));
      file.print(F("time(H:M:S)"));
      file.print(F(" "));
      file.print(F("jul_dat"));
      file.print(F(" "));
      file.print(F("temperature(C)"));
      file.print(F(" "));
      file.print(F("temp_min(C)"));
      file.print(F(" "));
      file.print(F("temp_max(C)"));
      file.print(F(" "));
      file.print(F("humidity(%)"));
      file.print(F(" "));
      file.print(F("hum_min(%)"));
      file.print(F(" "));
      file.print(F("hum_max(%)"));
      file.print(F(" "));
      file.print(F("pressure(hPa)"));
      file.print(F(" "));
      file.print(F("pres_min(hPa)"));
      file.print(F(" "));
      file.print(F("pres_max(hPa)"));
      file.print(F(" "));
      file.print(F("number_points"));
      file.println();
      file.close();      
    }
  
  file = SD.open(filename, FILE_WRITE);
  
    if (file) { 
      lcd.setCursor(10,0);
      lcd.print(F("."));
      #ifdef logSerial
        Serial.print(F("open ")); 
        Serial.println(filename);
      #endif 
      }
    else { 
      lcd.setCursor(10,0);
      lcd.print(F("!"));
      err=true;
      #ifdef logSerial
        Serial.print(F("error opening ")); 
        Serial.println(filename); 
      #endif
      #ifdef rebootSD
        reboot();
      #endif
      }   

  double jd=date2jd(year,mon,day,hour,minute,second);
   
  file.print(F("20"));
  file.print(year);
  file.print(F("-"));
  if (mon<10) { file.print(F("0")); }
  file.print(mon);
  file.print(F("-"));
  if (day<10) { file.print(F("0")); }
  file.print(day);
  file.print(F(" "));
  if (hour<10) { file.print(F("0")); }
  file.print(hour);
  file.print(F(":"));
  if (minute<10) { file.print(F("0")); }
  file.print(minute);
  file.print(F(":"));
  if (second<10) { file.print(F("0")); }
  file.print(second);
  file.print(F(" "));
  file.print(F("245"));
  file.print(jd,5);
  file.print(F(" "));
  file.print(tv[0]/n);
  file.print(F(" "));
  file.print(tv[1]);
  file.print(F(" "));
  file.print(tv[2]);
  file.print(F(" "));
  file.print(hv[0]/n);
  file.print(F(" "));
  file.print(hv[1]);
  file.print(F(" "));
  file.print(hv[2]);
  file.print(F(" "));
  file.print(Pv[0]/n);
  file.print(F(" "));
  file.print(Pv[1]);
  file.print(F(" "));
  file.print(Pv[2]);
  file.print(F(" "));
  file.print(n);
  file.println();
  file.close();
}

void dateTime(uint16_t* date, uint16_t* time) {
  byte second, minute, hour, dayOfWeek, dayOfMonth, month, year;
  
  // retrieve data from DS3231
  readDS3231time(&second, &minute, &hour, &dayOfWeek, &dayOfMonth, &month, &year);

  // return date using FAT_DATE macro to format fields
  *date = FAT_DATE(2000+year, month, dayOfMonth);

  // return time using FAT_TIME macro to format fields
  *time = FAT_TIME(hour, minute, second);
}

void teplota(float* t, float* h){
  //teplota+tlak 
  
  dht DHT;
  int readData=DHT.read22(dhtPin); // Reads the data from the sensor  read11 (DHT11) / read22 (DHT22)
  *t=DHT.temperature; // Gets the values of the temperature
  *h=DHT.humidity; // Gets the values of the humidity

  #ifdef logTemp
    Serial.print(F("Temp: ")); 
    Serial.print(*t); 
    Serial.print(F("; Hum: ")); 
    Serial.println(*h);
  #endif
}

double tlak(){
  char status;
  double T,P,p0;

  status = pressure.startTemperature();
  if (status != 0)
  {
    delay(status);
    status = pressure.getTemperature(T);
    if (status != 0)
    {
      #ifdef logBar
        Serial.print(F("temperature: "));
        Serial.print(T,2);
        Serial.println(F(" deg C"));
      #endif
      
      status = pressure.startPressure(3);
      if (status != 0)
      {
        // Wait for the measurement to complete:
        delay(status);
        
        status = pressure.getPressure(P,T);
        if (status != 0)
        {
          #ifdef logBar
            Serial.print(F("absolute pressure: "));
            Serial.print(P,2);
            Serial.println(F(" hPa"));
          #endif
      
          p0 = pressure.sealevel(P,ALTITUDE); 

          #ifdef logBar
            Serial.print(F("relative (sea-level) pressure: "));
            Serial.print(p0,2);
            Serial.println(F(" hPa"));
          #endif
          
          return p0;
        }
        #ifdef logBar
          else Serial.println(F("error retrieving pressure measurement"));
        #endif
      }
      #ifdef logBar
        else Serial.println(F("error starting pressure measurement"));
      #endif
    }
    #ifdef logBar
      else Serial.println(F("error retrieving temperature measurement"));
    #endif
  }
  #ifdef logBar
    else Serial.println(F("error starting temperature measurement"));
  #endif
  return 9999;  //chyba tlaku
}

void linReg(double *y, float *coef){
  //linearna regresia priamkou y=coef[0]+coef[1]*order
  double xy=0;
  double x2=0;
  double xS=0;
  double yS=0;

  byte j;
  
  for (j=0;j<i;j++) {
    xS+=j;
    yS+=y[j];
    x2+=pow(j,2);
    xy+=j*y[j];
  }

  coef[0]=(x2*yS-xS*xy)/(i*x2-pow(xS,2)); 
  coef[1]=(i*xy-xS*yS)/(i*x2-pow(xS,2));  
}

void setup() {  
  #ifdef logSerial || logBar || logTemp
    Serial.begin(9600);
  
    Serial.println(F("REBOOT"));
    
    //Tlakovy senzor
    if (pressure.begin())
      Serial.println(F("BMP180 init success"));
    else
    {
      Serial.println(F("BMP180 init fail\n\n"));
      //while(1); // Pause forever.
    }
  #endif
  
  #ifdef logSerial  
    //SD karta
    Serial.print(F("Initializing SD card..."));
    if (!SD.begin(chipSelect)) {
      Serial.println(F("Card failed, or not present"));
      err=true;
      //while (1);
    }
    Serial.println(F("card initialized."));
  #else
    pressure.begin();
    if (!SD.begin(chipSelect)) { err=true; }
  #endif

  if (!err) {
      SdFile::dateTimeCallback(dateTime);   //nastavenie casu pre SD kartu
      
      File file;
      char filename[]="reboot.log";

      file = SD.open(filename, FILE_WRITE);

      if (file) {
        byte second, minute, hour, dayOfWeek, dayOfMonth, month, year;
        // retrieve data from DS3231
        readDS3231time(&second, &minute, &hour, &dayOfWeek, &dayOfMonth, &month, &year);

        file.print(F("20"));
        file.print(year);
        file.print(F("-"));
        if (month<10) { file.print(F("0")); }
        file.print(month);
        file.print(F("-"));
        if (dayOfMonth<10) { file.print(F("0")); }
        file.print(dayOfMonth);
        file.print(F(" "));
        if (hour<10) { file.print(F("0")); }
        file.print(hour);
        file.print(F(":"));
        if (minute<10) { file.print(F("0")); }
        file.print(minute);
        file.print(F(":"));
        if (second<10) { file.print(F("0")); }
        file.print(second);
        file.println();
        file.close();        
      }
      else {
        err=true;  
      }

      //posun casu
      char filename1[]="dt";

      file = SD.open(filename1, FILE_READ);

      if (file) {
        int dt=0;
        dt=file.parseInt();
        file.close();

        //dt=1;
        #ifdef logSerial
          Serial.print(F("dt ")); 
          Serial.println(dt);
        #endif 

        if (dt!=0) {
          byte second, minute, hour, dayOfWeek, dayOfMonth, month, year;
          // retrieve data from DS3231
          readDS3231time(&second, &minute, &hour, &dayOfWeek, &dayOfMonth, &month, &year);
          #ifdef logSerial
            Serial.print(F("old time ")); 
            Serial.print(hour, DEC);
            // convert the byte variable to a decimal number when displayed
            Serial.print(F(":"));
            if (minute<10){
                Serial.print("0");
            }
            Serial.print(minute, DEC);
            Serial.print(F(":"));
            if (second<10){
                Serial.print("0");
            }
            Serial.print(second, DEC);
            Serial.println();
          #endif 

          if (dt>0) {
            int dm=int(dt/60);
            int ds=dt-dm*60;
            #ifdef logSerial
              Serial.print(dm);
              Serial.print(F(" "));
              Serial.println(ds);
            #endif
            second+=ds;
            minute+=dm; 
            if (minute>=60) {
              minute-=60;
              hour=hour+1;
            }
          }

          if (dt<0) {
            dt=-dt;
            int dm=int(dt/60);
            int ds=dt-dm*60;
            #ifdef logSerial
              Serial.print(dm);
              Serial.print(F(" "));
              Serial.println(ds);
            #endif
            if (ds>second) {
              second+=60;
              dm+=1;
            }
            if (dm>minute) {
              minute+=60;
              hour-=1;
            }
            second-=ds;
            minute-=dm; 
          }          

          #ifdef logSerial
            Serial.print(F("new time ")); 
            Serial.print(hour, DEC);
            // convert the byte variable to a decimal number when displayed
            Serial.print(F(":"));
            if (minute<10){
                Serial.print("0");
            }
            Serial.print(minute, DEC);
            Serial.print(F(":"));
            if (second<10){
                Serial.print("0");
            }
            Serial.print(second, DEC);
            Serial.println();
          #endif 

          setDS3231time(byte(second),byte(minute),hour,dayOfWeek,dayOfMonth,month,year);      

          SD.remove(filename1);   //zmazat stary "dt"
          file = SD.open(filename1, FILE_WRITE);
          file.write("0");
          file.close();
        }
      }
      
  }
  
  //LCD
  byte deg[] = {
    B00111,
    B00101,
    B00111,
    B00000,
    B00000,
    B00000,
    B00000,
    B00000
  };
  
  byte up[] = {
    B11111,
    B00011,
    B00101,
    B01001,
    B10001,
    B00000,
    B00000,
    B00000
  };
  
  byte down[] = {
    B00000,
    B00000,
    B00000,
    B10001,
    B01001,
    B00101,
    B00011,
    B11111
  };
  
  byte stat[] = {
    B01000,
    B00100,
    B00010,
    B11111,
    B00010,
    B00100,
    B01000,
    B00000
  };

  byte mini[] = {
    B00100,
    B00100,
    B00100,
    B00100,
    B10101,
    B01110,
    B00100,
    B11111
  };

  byte maxi[] = {
    B11111,
    B00100,
    B01110,
    B10101,
    B00100,
    B00100,
    B00100,
    B00100
  };

  lcd.init();
  lcd.backlight();
  lcd.createChar(0,down);
  lcd.createChar(1,stat);
  lcd.createChar(2,up);
  lcd.createChar(3,mini);
  lcd.createChar(4,maxi);
  lcd.createChar(6,deg);
}

void loop() {
  // put your main code here, to run repeatedly:  
    
  byte second, minute, hour, dayOfWeek, dayOfMonth, month, year;
  // retrieve data from DS3231
  readDS3231time(&second, &minute, &hour, &dayOfWeek, &dayOfMonth, &month, &year);

  if (!(oldDay==dayOfMonth)) {
    Pd[0]=9999;
    td[0]=100;
    hd[0]=100; 
    Pd[1]=0;
    td[1]=-100;
    hd[1]=0;  
    oldDay=dayOfMonth; 
  }
  
  //aktualne hodnoty
  double P;
  float t,h;
  
  if (counter>60) {
    //meranie raz za 60 cyklov = 30s
    P=tlak();
    //teplota+tlak  
    teplota(&t,&h);
    counter=0;

    n++;
    Pv[0]+=P;
    tv[0]+=t;
    hv[0]+=h;

    if (P<Pv[1]) {
      Pv[1]=P;
      if (P<Pd[0]) { Pd[0]=P; }
    } 
    if (P>Pv[2]) {
      Pv[2]=P;
      if (P>Pd[1]) { Pd[1]=P; }
   }

    if (t<tv[1]) {
      tv[1]=t;
      if (t<td[0]) {td[0]=t;}
    }
    if (t>tv[2]) {
      tv[2]=t;
      if (t>td[1]) {td[1]=t;}
    }

    if (h<hv[1]) {
      hv[1]=h;
      if (h<hd[0]) {hd[0]=h;}
    }
    if (h>hv[2]) {
      hv[2]=h;
      if (h>hd[1]) {hd[1]=h;}
    }    
  }

  byte typ=((second%20)/5);  //ine zobrazenie kazdych 5s - teraz/min/max
  if (typ<2) { zobraz(t,P,h,year,month,dayOfMonth,dayOfWeek,hour,minute,second,0); }
  else if (typ==2) { zobraz(td[0],Pd[0],hd[0],year,month,dayOfMonth,dayOfWeek,hour,minute,second,1); }
  else { zobraz(td[1],Pd[1],hd[1],year,month,dayOfMonth,dayOfWeek,hour,minute,second,2); }

  if ((output) and (minute%5==0) and (n>2)) {  //5minutovy interval -> SD
    byte j;
    if (i<12) {
      P_vals[i]=Pv[0]/n;
      t_vals[i]=tv[0]/n;
      h_vals[i]=hv[0]/n;
      i++;
    }
    else {      
      for (j=1;j<12;j++){
        P_vals[j-1]=P_vals[j];
        P_vals[11]=Pv[0]/n;
        t_vals[j-1]=t_vals[j];
        t_vals[11]=tv[0]/n;
        h_vals[j-1]=h_vals[j];
        h_vals[11]=hv[0]/n;
      }
    }

    if (i>3) {
      //vypocet trendu
      float coef[2];      
      
      linReg(P_vals,coef);      
      if (coef[1]<-0.02) {P_trend=0;}  //klesa tlak
      else if (coef[1]>0.02) {P_trend=2;}  //stupa tlak
      else {P_trend=1;}  //rovnaky tlak

      //porovnanie s prvou hodnotou
      //if ((P_vals[i-1]-P_vals[0])<-0.02) {P_trend=0;}  //klesa tlak
      //else if ((P_vals[i-1]-P_vals[0])>0.02) {P_trend=2;}  //stupa tlak
      //else {P_trend=1;}  //rovnaky tlak

      linReg(t_vals,coef);      
      if (coef[1]<-0.1) {t_trend=0;}  //klesa teplota
      else if (coef[1]>0.1) {t_trend=2;}  //stupa teplota
      else {t_trend=1;}  //rovnaka teplota

      //porovnanie s prvou hodnotou
      //if ((t_vals[i-1]-t_vals[0])<-0.1) {t_trend=0;}  //klesa teplota
      //else if ((t_vals[i-1]-t_vals[0])>0.1) {t_trend=2;}  //stupa teplota
      //else {t_trend=1;}  //rovnaka teplota

      linReg(h_vals,coef);
      if (coef[1]<-0.1) {h_trend=0;}  //klesa vlhkost
      else if (coef[1]>0.1) {h_trend=2;}  //stupa vlhkost
      else {h_trend=1;}  //rovnaka vlhkost

      //porovnanie s prvou hodnotou
      //if ((h_vals[i-1]-h_vals[0])<-0.1) {h_trend=0;}  //klesa vlhkost
      //else if ((h_vals[i-1]-h_vals[0])>0.1) {h_trend=2;}  //stupa vlhkost
      //else {h_trend=1;}  //rovnaka vlhkost
    }

    #ifdef rebootSD
        if (!err) {   //ak reboot + stale chyba -> nezapisuj!
    #endif
    zapis(year,month,dayOfMonth,hour,minute,second);
    #ifdef rebootSD
        }
    #endif
    
    output=false;
    n=0;
    Pv[0]=0;
    tv[0]=0;
    hv[0]=0;   
    Pv[1]=9999;
    tv[1]=100;
    hv[1]=100; 
    Pv[2]=0;
    tv[2]=-100;
    hv[2]=0;  
  }
  else if (minute%5>0) { output=true; }

  counter++;
  delay(450);
}
