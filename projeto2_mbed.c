// Código feito para rodar no MBED simulator

#include "mbed.h"
#include "C12832.h"
#include "Sht31.h"

Ticker t1;

C12832 lcd(SPI_MOSI, SPI_SCK, SPI_MISO, p8, p11); // declarei o lcd pra poder debugar
Sht31 sht31(I2C_SDA, I2C_SCL);                    // declaro os dois sensores (temp e nivel)
PwmOut led_motor(p23);                            // led que sinaliza o motor e o PWM nele
DigitalOut led_aquecedor(p25);                    // led que sinaliza o aquecedor
DigitalIn porta(p10);                             // Porta da maquina (1 fechada, 0 aberta)
DigitalIn ligaDesliga(p13);
DigitalIn iniciarPausar(p13);                     // botão de iniciar pausar a maquina
DigitalIn selec1(p17);                            // botão de seleciona 1
DigitalIn selec2(p18);                            // botão de seleciona 2

InterruptIn interIniPausar(p13);

// Usei o delay para colocar os tempos mas provavelmente vamos ter q usar interrupt (por causa do pause)

// Criei IDS para os tipos de operação:

// ID Programa  Enchimento    Molho   Centrifugação    Centr. ciclo   Centr.t total      Enxague       Secagem       Secagem
//              (Litros)   (segundos)  (potênciasPWM)   (segundos)         (s)        (n de vezes)  (temperatura)  (segundos)
//  0 Dia a Dia    50         10          50%         3 on + 1 off         20               3            40 C           15
//  1 Rápido       30          5          30%         3 on + 1 off         20               1            43 C            7
//  2 Coloridas    70         15          30%         2 on + 1 off         30               3             -              -
//  3 Brancas      70          5          40%         4 on + 1 off         25               3            47 C            9
//  4 Cama e banho 90         18          70%         4 on + 1 off         30               2            50 C           12
//  5 Delicadas    60         12          30%         2 on + 1 off         15               3            39 C           10
//  6 Pesado/Jeans 90         17          80%         4 on + 1 off         35               2            48 C           17
//  7 1 Hr Pronto  25          6          50%         3 on + 1 off         20               2            45 C            8

// 20ms para pwm
// enxague repete etapas (enchimento molho e centrifugação)

static int status = 0; // variavel que indica em qual operação a maquina está:
// 0 - não operando, 1 - enchimento/molho, 2 - centrifugação, 3 - enxague, 4 - secagem

static int ligado = 1;
static int selecaoMode = 0; //usa o iniciarPausar pra selecionar então uma flag pra não pausar na seleção
static int pause = 0; //indica se a maquina esta pausada ou não (começa pausada)
static int contLigarDesligar = 0;

// etapa 1
static int volume_enchimento[] = {50, 30, 70, 70, 90, 60, 90, 23}; // array com todas volumes (L) de água por ordem de ID
static int tempo_molho[] = {10, 5, 15, 5, 18, 12, 17, 6};          // array com todos tempos de molho por ordem de ID

// etapa 2
static int nro_enxagues[] = {3, 1, 3, 3, 2, 3, 2, 2}; // array com n de vezes de enxagues por ordem de ID

// etapa 3
static float DC[] = {0.5, 0.3, 0.3, 0.4, 0.7, 0.3, 0.8, 0.5};        // array com todos duty cycle (0-1) por ordem de ID
static int ciclos_on[] = {3, 3, 2, 4, 4, 2, 4, 3};                   // array com todos nros de ciclos on por ordem de ID
static int tempo_centrifugacao[] = {20, 20, 30, 25, 30, 15, 35, 20}; // array com todos tempos de centrifugacao total por ordem de ID

// etapa 4
static int temperatura_secagem[] = {40, 43, 0, 47, 50, 39, 48, 45}; // array com todas temperaturas (Cº) de secagem por ordem de ID
static int tempo_secagem[] = {15, 7, 0, 9, 12, 10, 17, 8};          // array com todos tempos de secagem por ordem de ID

void controleEstados();

void verificarPorta(){
    if(porta.read()==0){
        lcd.cls();
        lcd.locate(3, 3);
        lcd.printf("Feche a porta");
        while(porta.read()==0){
            wait_ms(250);
        }
    }
}


void interPause(){ //só troca o estado de pause, quem pausa é o ticker
    if(!selecaoMode){
        pause = !pause;
        wait_ms(50); //estava lendo duas vezes a mesma apertada
    }
}


int processo_molho(int id)
{
    float nivel = 0.0;
    lcd.cls();
    status = 1;

    lcd.locate(3, 13);
    lcd.printf("Programa necessita: %d L", volume_enchimento[id]);
    lcd.printf(" %d", status);

    while (nivel < volume_enchimento[id])
    {
        nivel = sht31.readHumidity();
        lcd.locate(3, 3);
        lcd.printf("Nivel: %.2f L", nivel);
        wait_ms(100);
    }

    verificarPorta();
    
    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("Roupas de molho");
    lcd.locate(3, 13);
    lcd.printf("Aguarde %d segundos", tempo_molho[id]);
    wait(tempo_molho[id]);

    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("Molho realizado ");
    lcd.locate(3, 13);
    lcd.printf("com sucesso! ");
    wait(4);
    return 1;
}

int processo_enxague()
{
    float nivel;
    lcd.cls();
    status = 3;

    lcd.locate(3, 13);
    lcd.printf("Programa necessita: 0 L");
    lcd.printf("  %d", status);

    do
    {
        nivel = sht31.readHumidity();
        lcd.locate(3, 3);
        lcd.printf("Nivel: %.2f L", nivel);
        wait_ms(100);
    } while (10.0 < nivel); // coloquei 10 L pq nao consigo ir a 0 com o mouse

     verificarPorta();

    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("Enxague realizada ");
    lcd.locate(3, 13);
    lcd.printf("com sucesso! ");
    wait(4);
    return 1;
}

int processo_centrifugacao(int id)
{
    int count = 0;

    lcd.cls();
    status = 3;
    led_motor.period(1);

    verificarPorta();
    
    lcd.locate(3, 3);
    lcd.printf("Realizando ");
    lcd.locate(3, 13);
    lcd.printf("Centrifugacao");
    
    
    do
    {
        led_motor = DC[id];
        wait(ciclos_on[id]);
        led_motor = 0;
        wait(1);
        count += ciclos_on[id] + 1;
        printf("Count = %d", count);
    } while (count < tempo_centrifugacao[id]);

    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("Centrifugacao realizada ");
    lcd.locate(3, 13);
    lcd.printf("com sucesso! ");
    wait(4);
    return 1;
}

int processo_secagem(int id)
{
    float temp = 0.0;
    lcd.cls();
    status = 4;

    lcd.locate(3, 13);
    lcd.printf("Programa necessita: %d C", temperatura_secagem[id]);
    lcd.printf(" %d", status);

    led_aquecedor = 1;

    while (temp < temperatura_secagem[id])
    {
        temp = sht31.readTemperature();
        lcd.locate(3, 3);
        lcd.printf("Temperatura: %.2f C", temp);
        wait_ms(100);
    }

    verificarPorta();

    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("Secando roupas");
    lcd.locate(3, 13);
    lcd.printf("Aguarde %d segundos", tempo_secagem[id]);
    wait(tempo_secagem[id]);

    led_aquecedor = 0;

    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("Secagem realizada ");
    lcd.locate(3, 13);
    lcd.printf("com sucesso! ");
    wait(4);
    return 1;
}

void printSelecao(int s){
    //poderia ter uma matriz de string com os nomes
    //não precisaria dessa função dai
    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("Iniciando Programa");
    lcd.locate(3, 13);
    switch(s){
        case 0:
        lcd.printf("Dia a dia");
        break;
        case 1:
        lcd.printf("Rapido");
        break;
        case 2:
        lcd.printf("Coloridas");
        break;
        case 3:
        lcd.printf("Brancas");
        break;
        case 4:
        lcd.printf("Cama e Banho");
        break;
        case 5:
        lcd.printf("Delicadas");
        break;
        case 6:
        lcd.printf("Pesado/jeans");
        break;
        case 7:
        lcd.printf("1 Hora pronto");
        break;
    }
    wait(2);
    
}
void alterarCentrifugacao(){
    int selecao = 0;
    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("Inicar/Pausar p/ selecionar");
    wait_ms(1500);
    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("1-Proximo 2-Anterior ");
    while(iniciarPausar.read()==0){
        if(selec1.read() == 1 && selecao < 7)
            selecao++;
        if(selec2.read() == 1 && selecao > 0)
            selecao--;

        lcd.locate(3 ,13);
        lcd.printf("Selecionado: %d ", selecao + 1);
        
        wait_ms(150);
    }
    
    float temp=0;
    wait_ms(500);
    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("Tempo:");
    while(iniciarPausar.read()==0){
        if(selec1.read() == 1 && (tempo_centrifugacao[selecao]+temp) < 120.0 )
            temp++;
        if(selec2.read() == 1 && (tempo_centrifugacao[selecao]+temp) > 3.0 )
            temp--;
    
        lcd.locate(3, 13);
        lcd.printf("%3.1fs, 1-Aumenta 2-Diminui",tempo_centrifugacao[selecao]+temp);
    
        wait_ms(50);
    }
    tempo_centrifugacao[selecao] += temp;
    
    temp=0;
    wait_ms(500);
    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("Potencia:");
    while(iniciarPausar.read()==0){
        if(selec1.read() == 1 && (DC[selecao]+temp) < 0.9 )
            temp+=0.05;
        if(selec2.read() == 1 && (DC[selecao]+temp) > 0.2 )
            temp-=0.05;
    
        lcd.locate(3, 13);
        lcd.printf("%3.1f%%, 1-Aumenta 2-Diminui",100*(DC[selecao]+temp));
    
        wait_ms(50);
    }
    DC[selecao]+=temp;
    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("Configuracoes");
    lcd.locate(3, 13);
    lcd.printf("Alteradas");
    wait_ms(1750);
    
}

void perguntaAlterarCentrifugacao(){
    int ok = 0;
    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("Alterar as config.");
    lcd.locate(3, 13);
    lcd.printf("de centrifugacao? 1-S 2-N");
    while(!ok){
        if(selec1.read()==1){
            selecaoMode=1;
            alterarCentrifugacao();
            selecaoMode=0;
            ok=1;
        }
        if(selec2.read()==1)
            ok=1; 
        wait_ms(100);
    }
    wait(2);
}

int escolhaOperacao(){
    int selecao = 0;
    selecaoMode = 1; //para quando apertar o iniciar pausa de escolha sair do interrupt do botão sem mudar o pause
    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("Selecione o programa,");
    lcd.locate(3, 13);
    lcd.printf("Inicar/Pausar p/ comecar");
    wait_ms(1750);
    
    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("1-Proximo 2-Anterior");
    
    while(iniciarPausar.read()==0){
        if(selec1.read() == 1 && selecao < 7)
            selecao++;
        if(selec2.read() == 1 && selecao > 0)
            selecao--;

        lcd.locate(3 ,13);
        lcd.printf("Selecionado: %d ", selecao + 1);
        
        wait_ms(100);
    }
    
    printSelecao(selecao);
    
    selecaoMode = 0; //interrupt do botão volta a funcionar
    
    return selecao;
}

int main()
{   
    t1.attach(callback(&controleEstados), 0.5f);
    int start = 1, i, id_operacao;
    
    //pause não funcionando, inutil por enquanto
    interIniPausar.fall(callback(&interPause));

    while (1)
    {
        perguntaAlterarCentrifugacao();
        
        id_operacao = escolhaOperacao();
        if (start)
        {

            for (i = 0; i < nro_enxagues[id_operacao]; i++)
            {
                processo_molho(id_operacao);
                processo_centrifugacao(id_operacao);
                processo_enxague();
                // start = 0;
            }
            processo_secagem(id_operacao);
        }
        wait(1);
        lcd.cls();
        lcd.locate(3, 3);
        lcd.printf("Programa Finalizado!");
    }
}

/*
    contIniciarPausar = 0;
    contLigarDesligar = 0;
*/
void controleEstados(){
    //ticker de 1 seg que conta os seg que um botaõ foi pressionado e se esta em pause
   
   /* //pause não esta funcionando mt bem
        // acabe travando tudo
    if(pause){
        lcd.locate(110, 1);
        lcd.printf("P"); //coloca um P no canto superior direito mostrando q esta pausado
        
        //TODO 
        //Rotina para desligar motores,... (pg 11 do pdf do trab)
        
        while(pause)
            wait_ms(60);

        lcd.locate(110, 1);
        lcd.printf(">");
    }
    */
    
    //Já esta aqui o liga desliga mas não faz nada ainda
    if(ligaDesliga.read() == 1)
        contLigarDesligar++;
    else
        contLigarDesligar=0;
    //como o ticker é de 0.5seg tem q ser 6 e 4
    if(ligado && contLigarDesligar==6){
        ligado = 0;
        contLigarDesligar = 0;
    }
    if(!ligado && contLigarDesligar==4){
        ligado = 1;
        contLigarDesligar = 0;
    }

}






