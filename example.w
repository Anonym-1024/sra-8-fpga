




byte a;                 
byte b = 10;            
byte c = 0x1F, d = 'x'; 

a = b + 3;              
c = a - b;
d = c & 0x0F;
a = a + 1;              
b = (a + c) * 2;        
c = b - a - d;          

int8 t = -20;           
int8 u;
u = t + 5;              
u = -u;                 
u = u >> 1;             
d = d >> 1;             

int16 dx = -300;        
int16 dy = 0x0100;
dx = dx + dy;           
dy = dy - dx;           
dy = dy >> 1;           

addr p = 0x0400;        
addr q;
q = p + 0x100;
q = q + a;              
dx = dx + t;            
                        
a = dx;                 






volatile byte rx_ready = 0;
volatile byte rx_byte;



volatile byte tx_busy = 0;


volatile byte ticks = 0;
volatile int16 encoder = 0;     




byte got;
got = rx_ready;         
got = rx_ready;         

byte ch;
ch = rx_byte + 1;       

tx_busy = 1;            
tx_busy = 0;            

ticks = ticks + 1;      

int16 pos;
pos = encoder;          
                        
                        
                        
                        

byte snapshot;
snapshot = ticks;       
a = snapshot * 3;
b = snapshot * 3;       
                        