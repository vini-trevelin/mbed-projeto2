// Código feito para rodar no MBED simulator

#include "mbed.h"
#include "C12832.h"
#include "Sht31.h"

Ticker t1;

// pinagem baseada na imagem do professor
C12832 lcd(SPI_MOSI, SPI_SCK, SPI_MISO, p8, p11); // declarei o lcd pra poder debugar
Sht31 sht31(I2C_SDA, I2C_SCL);                    // declaro os dois sensores (temp e nivel)
PwmOut led_motor(p23);                            // led que sinaliza o motor e o PWM nele
DigitalOut led_aquecedor(p25);                    // led que sinaliza o aquecedor
DigitalIn porta(p10);                             // Porta da maquina (1 fechada, 0 aberta)

DigitalIn ligaDesliga(p12);
DigitalIn iniciarPausar(p13); // botão de iniciar pausar a maquina
DigitalIn hrPronto(p14);      // botão 1hr pronto
DigitalIn voltoLogo(p16);     // botão VOLTO LOGO
DigitalIn selec1(p17);        // botão de seleciona 1
DigitalIn selec2(p18);        // botão de seleciona 2

InterruptIn interIniPausar(p13);
// InterruptIn interVoltoLogo(p16);
InterruptIn interLigDeslig(p12);
InterruptIn interPortaAber(p10);
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

int aux = 10; // tempo em segundos para pergunto sobre volto logo
static int id;
static int voltoL = 0;
static int ligado = 0;
static int selecaoMode = 0; // usa o iniciarPausar pra selecionar então uma flag pra não pausar na seleção
static int pause = 0;       // indica se a maquina esta pausada ou não (começa pausada)
static int contLigarDesligar = 0;
static int continuar = 1; // quando aperta ligDeslig é pra não terminar o programa
static char modos[8][14] = {"Dia a Dia", "Rapido", "Coloridas", "Brancas", "Cama e Banho", "Delicadas", "Pesado/Jeans", "1 Hr pronto"};
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

void verificarPorta()
{
    while (porta.read() == 0)
    {
        lcd.cls();
        lcd.locate(3, 3);
        lcd.printf("Feche a porta");
        wait_ms(250);
    }
    wait_ms(25);
    lcd.cls();
    wait_ms(25);
}

void interPause()
{ // só troca o estado de pause, quem pausa é o ticker
    if (!selecaoMode && status != 0 && ligado)
    {
        pause = !pause;
        wait_ms(50); // estava lendo duas vezes a mesma apertada
    }
}

void interLD()
{
    /*
    Apertando ligDeslig no meio de um processo vai cancela-lo
    Saindo daquele for q vai realizando cada parte
    continuar volta a ser 1 antes de entrar no for
    */
    continuar = 0;
}

void interPorta()
{ // para pedir que a porta seja fechada mesmo no meio de um processo
    if (status != 0)
        verificarPorta();
}

int processo_molho(int id)
{
    float nivel = 0.0;
    lcd.cls();
    status = 1;

    verificarPorta();

    // tive q colocar tudo no while pra não bugar a tela quando voltar do paus
    while (nivel < volume_enchimento[id])
    {
        lcd.cls();
        lcd.locate(3, 13);
        lcd.printf("Programa necessita: %d L", volume_enchimento[id]);

        nivel = sht31.readHumidity();
        lcd.locate(3, 3);
        lcd.printf("Nivel: %.2f L", nivel);
        wait_ms(100);
    }

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
    status = 3;
    do
    {
        lcd.cls();
        lcd.locate(3, 13);
        lcd.printf("Programa necessita: 0 L");

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
    status = 2;
    led_motor.period(1);

    verificarPorta();

    do
    {
        lcd.cls();
        lcd.locate(3, 3);
        lcd.printf("Realizando ");
        lcd.locate(3, 13);
        lcd.printf("Centrifugacao");

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
    while (temp < temperatura_secagem[id])
    {
        lcd.cls();
        lcd.locate(3, 13);
        lcd.printf("Programa necessita: %d C", temperatura_secagem[id]);
        temp = sht31.readTemperature();
        lcd.locate(3, 3);
        lcd.printf("Temperatura: %.2f C", temp);
        wait_ms(100);
    }

    verificarPorta();
    led_aquecedor = 1;

    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("Secando roupas");
    lcd.locate(3, 13);
    lcd.printf("Aguarde %d segundos", tempo_secagem[id]);
    wait(tempo_secagem[id]);

    status = 0; // para quando voltar de um pause saber q naõ precisa ligar mais o aquecedor
    led_aquecedor = 0;

    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("Secagem realizada ");
    lcd.locate(3, 13);
    lcd.printf("com sucesso! ");
    wait(4);
    return 1;
}

void alterarCentrifugacao()
{
    int selecao = 0;
    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("Inicar/Pausar p/ selecionar");
    wait_ms(1500);
    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("1-Proximo 2-Anterior ");
    while (iniciarPausar.read() == 0)
    {
        if (selec1.read() == 1 && selecao < 7)
            selecao++;
        if (selec2.read() == 1 && selecao > 0)
            selecao--;

        lcd.locate(3, 13);
        lcd.printf("Selecionado: %d ", selecao + 1);

        wait_ms(150);
    }

    float temp = 0;
    wait_ms(500);
    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("Tempo:");
    while (iniciarPausar.read() == 0)
    {
        if (selec1.read() == 1 && (tempo_centrifugacao[selecao] + temp) < 120.0)
            temp++;
        if (selec2.read() == 1 && (tempo_centrifugacao[selecao] + temp) > 3.0)
            temp--;

        lcd.locate(3, 13);
        lcd.printf("%3.1fs, 1-Aumenta 2-Diminui", tempo_centrifugacao[selecao] + temp);

        wait_ms(50);
    }
    tempo_centrifugacao[selecao] += temp;

    temp = 0;
    wait_ms(500);
    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("Potencia:");
    while (iniciarPausar.read() == 0)
    {
        if (selec1.read() == 1 && (DC[selecao] + temp) < 0.9)
            temp += 0.05;
        if (selec2.read() == 1 && (DC[selecao] + temp) > 0.2)
            temp -= 0.05;

        lcd.locate(3, 13);
        lcd.printf("%3.1f%%, 1-Aumenta 2-Diminui", 100 * (DC[selecao] + temp));

        wait_ms(100);
    }
    DC[selecao] += temp;
    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("Configuracoes");
    lcd.locate(3, 13);
    lcd.printf("Alteradas");
    wait_ms(1750);
}

void perguntaAlterarCentrifugacao()
{
    int ok = 0;
    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("Alterar as config.");
    lcd.locate(3, 13);
    lcd.printf("de centrifugacao? 1-S 2-N");
    while (!ok)
    {
        if (selec1.read() == 1)
        {
            selecaoMode = 1;
            alterarCentrifugacao();
            selecaoMode = 0;
            ok = 1;
        }
        if (selec2.read() == 1)
            ok = 1;
        wait_ms(100);
    }
    wait(2);
}

int escolhaOperacao()
{
    int selecao = 0;
    selecaoMode = 1; // para quando apertar o iniciar pausa de escolha sair do interrupt do botão sem mudar o pause
    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("Selecione o programa,");
    lcd.locate(3, 13);
    lcd.printf("Inicar/Pausar p/ comecar");
    wait_ms(1750);

    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("1-Proximo 2-Anterior");

    while (iniciarPausar.read() == 0)
    {

        if (hrPronto.read() == 1)
            return 7;

        if (selec1.read() == 1 && selecao < 7)
            selecao++;
        if (selec2.read() == 1 && selecao > 0)
            selecao--;

        lcd.locate(3, 13);
        lcd.printf("Selecionado: %d", selecao + 1);

        wait_ms(100);
    }

    while (aux != 0)
    {
        lcd.locate(3, 3);
        lcd.printf("Volto Logo? (%d segundos)", aux);
        if (voltoLogo.read() == 1 && voltoL == 0)
            voltoL = 1;

        if (voltoLogo.read() == 0 && voltoL == 0)
            voltoL = 0;

        lcd.locate(3, 13);
        lcd.printf("(1) ON (0) OFF --> %d", voltoL);

        wait_ms(1000);
        aux--;
    }
    aux = 10;

    lcd.cls();
    lcd.locate(3, 3);
    lcd.printf("Iniciando Programa");
    lcd.locate(3, 13);
    lcd.printf("%s", modos[selecao]);

    wait(2);

    selecaoMode = 0; // interrupt do botão volta a funcionar
    id = selecao;    // pro pause
    return selecao;
}

int main()
{
    t1.attach(callback(&controleEstados), 0.5f);
    int start = 1, i, id_operacao;

    interIniPausar.fall(callback(&interPause));
    interLigDeslig.fall(callback(&interLD));

    // Buga mais ainda os tempos
    // interPortaAber.fall(callback(&interPorta));
    // interPortaAber.rise(callback(&interPorta));

    // aparentemente essas interrupções bugam os tempos do sistema, delay fica todo  errado
    // interVoltoLogo.fall(callback(&interVL));
    printf("Iniciando");
    while (1)
    {

        // gostaria de fazer q se segurar o botão 1 por x segs entra no modo de configurar
        // só poderia entrar no modo de configurar de o status = 0 (não operando)
        if (ligado)
        {
            perguntaAlterarCentrifugacao();
            id_operacao = escolhaOperacao();
            continuar = 1;
            for (i = 0; i < nro_enxagues[id_operacao]; i++)
            {
                processo_molho(id_operacao);

                if (!continuar)
                    break;

                processo_centrifugacao(id_operacao);

                if (!continuar)
                    break;

                processo_enxague();

                if (!continuar)
                    break;
            }
            if (continuar)
                processo_secagem(id_operacao);

            status = 0;
            wait(1);
            lcd.cls();
            lcd.locate(3, 3);
            lcd.printf("Programa Finalizado!");
            wait(2);
        }

        if (voltoL == 1)
        {
            wait(1);
            lcd.cls();
            lcd.locate(3, 3);
            lcd.printf("Volto Logo ativado!");
            lcd.locate(3, 13);
            lcd.printf("(X)  Btn Volto Logo");
            wait(2);
            do
            {
                // como não consegui usar interrupt, tem que ficar clicando no botão pra desligar
                led_motor = 0.3;
                wait(7);
                led_motor = 0;
                wait(14); // deixei 14 s para testar (mudar pra 1 min)
            } while (voltoLogo.read() == 0);
            voltoL = 0;
            lcd.cls();
            lcd.locate(3, 3);
            lcd.printf("Volto Logo desativado!");
            wait(3);
        }

        // Se não tiver com volto logo é pra desligar
        // então fica preso ali no volto logo
        // quando sair desliga aqui
        // controleEstados desliga ela
        ligado = 0;
        wait_ms(10);
        lcd.cls();
    }
}

/*
    contIniciarPausar = 0;
    contLigarDesligar = 0;
*/

void entraPause()
{
    /*
    Não acho q tem mais coisa pra desligar,
    mas se tiver só add e prestar atenção se precisa explicitamente ligar de novo
    no saiPause()
    */
    led_aquecedor = 0;
    led_motor = 0;
}

void saiPause()
{
    /*
    Coisas que precisamos ligar de novo
    Só percebi o aquecedor pq caso esteja esperando a secagem e tenha um pause
    na saida do pause estaria desligado o aquecedor.

    O motor por exemplo fica ligando e desligado sozinho em um loop
    então na volta de um pause não teria problema

    tb pede pra colocar os niveis corretos

    */
    float nivel = 0, temp = 0;

    switch (status)
    {
    case 1:
        lcd.cls();
        while (nivel < volume_enchimento[id])
        {
            lcd.locate(3, 13);
            lcd.printf("Programa necessita: %d L", volume_enchimento[id]);

            nivel = sht31.readHumidity();
            lcd.locate(3, 3);
            lcd.printf("Nivel: %.2f L", nivel);
            wait_ms(100);
        }
        break;

        /*case 2:
        //não precisa retomar nada da centrifugação
        break;*/

    case 3:
        while (10.0 < nivel)
        {
            lcd.locate(3, 13);
            lcd.printf("Programa necessita: 0 L");

            nivel = sht31.readHumidity();
            lcd.locate(3, 3);
            lcd.printf("Nivel: %.2f L", nivel);
            wait_ms(100);
        }
        break;

    case 4:
        while (temp < temperatura_secagem[id])
        {
            lcd.locate(3, 13);
            lcd.printf("Programa necessita: %d C", temperatura_secagem[id]);

            led_aquecedor = 1;

            temp = sht31.readTemperature();
            lcd.locate(3, 3);
            lcd.printf("Temperatura: %.2f C", temp);
            wait_ms(100);
        }
        break;
    default:
        break;
    }
}

void controleEstados()
{
    if (pause)
    {

        entraPause();
        wait_ms(10);
        lcd.cls();
        lcd.locate(5, 3);
        lcd.printf("Sistema pausado");
        while (pause)
        {
            wait_ms(500);
        }
        lcd.cls();
        lcd.locate(3, 3);
        lcd.printf("Sistema despausado");
        lcd.locate(3, 13);
        // 0 - n operando , 1 - enchimento/molho, 2 - centrifugação, 3 - enxague, 4 - secagem
        char processos[5][16] = {"", ": Molho", ": Centrifugacao", ": Enxague", ": Secagem"};
        lcd.printf("Retomando%s", processos[status]);
        lcd.cls();
        saiPause();
        wait(2);
        lcd.cls();
    }

    if (ligaDesliga.read() == 1)
        contLigarDesligar++;
    else
        contLigarDesligar = 0;
    // como o ticker é de 0.5seg tem q ser 6 e 4

    if (ligado && contLigarDesligar >= 6)
    {
        ligado = 0;
        contLigarDesligar = 0;
    }
    if (!ligado && contLigarDesligar >= 4)
    {
        ligado = 1;
        contLigarDesligar = 0;
    }

    if (ligado == 0)
    {
        lcd.cls();
        entraPause();
    }
}