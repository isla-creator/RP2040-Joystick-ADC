#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "font.h"
#include "hardware/pio.h"
#include "hardware/i2c.h"
#include "ssd1306.h"
#include "rgb.h"
#include "buttons.h"

// Definições dos pinos para comunicação I2C
#define I2C_PORT i2c1         // Porta I2C utilizada
#define I2C_SDA 14            // Pino para SDA (Serial Data Line)
#define I2C_SCL 15            // Pino para SCL (Serial Clock Line)
#define ENDERECO 0x3C         // Endereço I2C do display SSD1306

// Definições de pinos

#define VRX_PIN 26            // Pino analógico para eixo X
#define VRY_PIN 27            // Pino analógico para eixo Y

ssd1306_t ssd;  // Estrutura global para controle do display

// Inicializa o I2C, o display OLED SSD1306 e o ADC do joystick
void display() {
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    ssd1306_init(&ssd, WIDTH, HEIGHT, true, ENDERECO, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_fill(&ssd, false); // Limpa o display (fundo preto)
    ssd1306_send_data(&ssd);

    adc_init();
    adc_gpio_init(VRX_PIN); // Eixo X
    adc_gpio_init(VRY_PIN); // Eixo Y
}

// Inicializa o PWM em um GPIO específico
uint pwm_init_gpio(uint gpio, uint wrap) {
    gpio_set_function(gpio, GPIO_FUNC_PWM);  // Configura o pino como PWM
    uint slice_num = pwm_gpio_to_slice_num(gpio);  // Obtém o slice do PWM
    pwm_set_wrap(slice_num, wrap);  // Define o valor de wrap (TOP)
    pwm_set_enabled(slice_num, true);  // Ativa o PWM
    return slice_num;
}

// Variável global para alternar entre estilos de borda
int borda_estado = 0;  // 0 = borda simples, 1 = borda dupla, 2 = sem borda

void desenhar_borda() {
    switch (borda_estado) {
        case 0: // Borda simples
            ssd1306_rect(&ssd, 3, 3, 122, 60, true, false);
            break;
        case 1: // Borda dupla
            ssd1306_rect(&ssd, 3, 3, 122, 60, true, false);
            ssd1306_rect(&ssd, 5, 5, 118, 54, true, false);
            break;
        case 2: // Sem borda
            // Sem borda, apenas fundo preto
            break;
    }
}

// Função de debounce para os botões
void debounce_botao(uint pino, volatile uint32_t *last_irq_time, bool *estado_LED) {
    uint32_t tempo_atual = to_ms_since_boot(get_absolute_time());

    if (tempo_atual - *last_irq_time > DEBOUNCE_DELAY) {
        *last_irq_time = tempo_atual;
        *estado_LED = !(*estado_LED);

        if (pino == BOTAO_A) {
            // Alterna entre ativar ou desativar o PWM dos LEDs vermelho e azul
            if (*estado_LED) {
                pwm_set_gpio_level(red, 4095);
                pwm_set_gpio_level(blue, 4095);
            } else {
                pwm_set_gpio_level(red, 0);
                pwm_set_gpio_level(blue, 0);
            }
        } else if (pino == BOTAO_B) { // Botão B - alterna o LED verde e a borda
            gpio_put(green, *estado_LED); // Em vez de usar state(), diretamente alteramos o pino GREEN

            // Alterna o estilo da borda
            borda_estado = (borda_estado + 1) % 3;
        }
    }
}

// Callback chamado quando ocorre interrupção em algum botão
void botao_callback(uint gpio, uint32_t eventos) {
  if (gpio == BOTAO_A) { // Botão A - ativa/desativa o PWM dos LEDs
    debounce_botao(BOTAO_A, &last_irq_time_A, &estado_LED_A);
  } else if (gpio == BOTAO_B) { // Botão B - alterna o LED verde
    debounce_botao(BOTAO_B, &last_irq_time_B, &estado_LED_B);
  }
}

// Converte o valor do joystick (0 a 4095) em valor PWM, usando o desvio em relação ao centro
uint16_t joystick_to_pwm(uint16_t joystick_value) {
    int32_t offset = (int32_t)joystick_value - 2048;  // Diferença em relação ao centro
    if (offset < 0) offset = -offset;                  // Usa valor absoluto
    return (uint16_t)(offset * 2);                     // Escala para faixa aproximada de 0 a 4095
}

int main() {
    stdio_init_all();  // Inicializa a comunicação serial
    iniciar_botoes();
    iniciar_rgb();
    adc_init();
    adc_gpio_init(VRX_PIN);
    adc_gpio_init(VRY_PIN);

    uint pwm_wrap = 4095;
    pwm_init_gpio(red, pwm_wrap);
    pwm_init_gpio(blue, pwm_wrap);

    display();  // Inicializa o display e o ADC

    const int square_size = 8;
    int centro_x = (WIDTH - square_size ) / 2;
    int centro_y = (HEIGHT - square_size ) / 2;

    while (true) {
        // Leitura do eixo X
        adc_select_input(0);
        uint16_t x_value = adc_read();

        // Leitura do eixo Y
        adc_select_input(1);
        uint16_t y_value = adc_read();

        // Calcula os valores PWM com base no joystick
        uint16_t pwm_red = joystick_to_pwm(x_value);
        uint16_t pwm_blue = joystick_to_pwm(y_value);

        // Se o estado do PWM estiver ativo (estado_LED_A == true), atualiza os níveis conforme o joystick;
        // caso contrário, define nível 0 (LEDs apagados).
        if (estado_LED_A) {
            pwm_set_gpio_level(red, pwm_red);
            pwm_set_gpio_level(blue, pwm_blue);
        }

        // Mapeamento do joystick para a posição do quadrado:
        int pos_x = centro_x + (( 2048 - (int)x_value ) * centro_x)  /2048;
        int pos_y = centro_y + (( 2048 - (int)y_value ) * centro_y) / 2048;

        // Atualiza o display:
        ssd1306_fill(&ssd, false);  // Limpa (fundo preto)
        desenhar_borda(); // Atualiza a borda conforme o estado atual
        ssd1306_rect(&ssd, pos_y, pos_x, square_size, square_size, true, true);  // Desenha o quadrado preenchido
        ssd1306_send_data(&ssd);
        
        printf("(x, y) = (%u%%, %u%%)  ", x_value * 100 / 4094, y_value * 100 / 4094);
        printf("Brilho(led azul, led vermelho) = (%u%%, %u%%)\n", pwm_blue * 100 / 4094, pwm_red * 100 / 4094);
        sleep_ms(100);
    }

    return 0;
}