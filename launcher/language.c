/* The app in Brazilian Portuguese: a translation for each English text it
 * shows, the choice kept between runs, and the PS4's own system language. */
#include "language.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "draw.h"
#include "screen.h"

/* libSceSystemService, declared here so this file needs no SDK header. */
int sceSystemServiceParamGetInt(int id, int *value);
#define C4F_SYSTEM_PARAM_LANG 1
#define C4F_SYSTEM_LANG_PORTUGUESE 7
#define C4F_SYSTEM_LANG_PORTUGUESE_BR 17

#define C4F_LANGUAGE_DIR "/data/control4free"
#define C4F_LANGUAGE_FILE C4F_LANGUAGE_DIR "/launcher-language"

static const char *const c4fLanguageNames[C4F_LANGUAGE_COUNT] = { "auto", "en", "pt-BR" };

int c4fLanguageLoad(void)
{
    char word[16] = {0};
    FILE *f = fopen(C4F_LANGUAGE_FILE, "r");
    if (!f) return C4F_LANGUAGE_AUTO;
    if (!fgets(word, sizeof(word), f)) word[0] = 0;
    fclose(f);
    word[strcspn(word, "\r\n ")] = 0;
    for (int i = 0; i < C4F_LANGUAGE_COUNT; i++)
        if (!strcmp(word, c4fLanguageNames[i])) return i;
    return C4F_LANGUAGE_AUTO;
}

void c4fLanguageSave(int choice)
{
    if (choice < 0 || choice >= C4F_LANGUAGE_COUNT) return;
    (void)mkdir(C4F_LANGUAGE_DIR, 0777);
    FILE *f = fopen(C4F_LANGUAGE_FILE, "w");
    if (!f) { printf("[c4f-launcher] could not save the language\n"); return; }
    fprintf(f, "%s\n", c4fLanguageNames[choice]);
    fclose(f);
}

static int c4fSystemPortuguese(void)
{
    int language = -1;
    if (sceSystemServiceParamGetInt(C4F_SYSTEM_PARAM_LANG, &language) < 0) return 0;
    return language == C4F_SYSTEM_LANG_PORTUGUESE || language == C4F_SYSTEM_LANG_PORTUGUESE_BR;
}

void c4fLanguageUse(int choice)
{
    static int used = -1;
    int portuguese = choice == C4F_LANGUAGE_PORTUGUESE || (choice == C4F_LANGUAGE_AUTO && c4fSystemPortuguese());
    if (portuguese == used) return;
    used = portuguese;
    c4fSetTextTranslation(portuguese ? c4fPortugueseText : NULL);
}

/* ---- the translations ---- */

/* A '%' conversion in the English stands for a value the app filled in; the
 * Portuguese has the same conversions in the same order. */
typedef struct { const char *en, *pt; } C4fPhrase;

static const C4fPhrase c4fPortuguese[] = {
    /* Main screen */
    { "Not responding", "Sem resposta" },
    { "Check Control4Free", "Verifique o Control4Free" },
    { "Something answers on port 4264 but not the way this app expects. If an older Control4Free is running, stop it on its controller page or restart the PS4.",
      "Algo responde na porta 4264, mas não do jeito que este app espera. Se um Control4Free mais antigo estiver rodando, pare-o pela página do controle ou reinicie o PS4." },
    { "App error", "Erro no app" },
    { "Reopen the app", "Reabra o app" },
    { "The app could not start properly. Close it with the PS button and open it again. Control4Free itself is not affected.",
      "O app não conseguiu iniciar direito. Feche-o com o botão PS e abra de novo. O Control4Free em si não foi afetado." },
    { "Working", "Trabalhando" },
    { "Please wait", "Aguarde" },
    { "Checking", "Verificando" },
    { "1 controller connected", "1 controle conectado" },
    { "%d controllers connected", "%d controles conectados" },
    { "Running", "Rodando" },
    { "Control4Free is running", "O Control4Free está rodando" },
    { "Open the address or scan the code on your phone or PC. Pick a controller there, then choose its user on the PS4.",
      "Abra o endereço ou escaneie o código no seu celular ou PC. Escolha um controle lá e depois o usuário dele no PS4." },
    { "Stopped", "Parado" },
    { "Start Control4Free", "Iniciar o Control4Free" },
    { "It starts with GoldHEN after each restart. To start now, enable GoldHEN's PayLoader Server and press Cross.",
      "Ele inicia com o GoldHEN após cada reinício. Para iniciar agora, ative o PayLoader Server do GoldHEN e aperte X." },
    { "Not running", "Não está rodando" },
    { "Set up Control4Free", "Configurar o Control4Free" },
    { "Press Cross to set up auto-start. To start Control4Free now, enable GoldHEN's PayLoader Server.",
      "Aperte X para configurar o início automático. Para iniciar o Control4Free agora, ative o PayLoader Server do GoldHEN." },
    { "Open on your phone or PC", "Abra no seu celular ou PC" },
    { "Connect the PS4 to your home network", "Conecte o PS4 à sua rede doméstica" },
    { "Use the same network as the PS4. Touch, a keyboard and gamepads connected to the phone or PC all work.",
      "Use a mesma rede do PS4. Toque, teclado e gamepads conectados ao celular ou PC funcionam." },
    { "Working...", "Trabalhando..." },
    { "Scan with your phone's camera", "Escaneie com a câmera do celular" },
    { "The code appears once it runs", "O código aparece quando ele estiver rodando" },
    { "Installed %s; running %s", "Instalado %s; rodando %s" },
    { "Press Square, then Cross to stop, and Cross again to start this app's version. All players disconnect.",
      "Aperte Quadrado e depois X para parar, e X de novo para iniciar a versão deste app. Todos os jogadores serão desconectados." },
    { "Starts by itself with GoldHEN", "Inicia sozinho com o GoldHEN" },
    { "GoldHEN starts Control4Free each time it loads. Closing this app leaves your controllers running. Turn off other controller plugins in your games.",
      "O GoldHEN inicia o Control4Free sempre que carrega. Fechar este app mantém os controles ativos. Desative outros plugins de controle." },
    { "Auto-start uses an older Control4Free", "O início automático usa um Control4Free mais antigo" },
    { "Press Triangle to update auto-start. GoldHEN uses this version after the next restart.",
      "Aperte Triângulo para atualizar o início automático. O GoldHEN usa esta versão após o próximo reinício." },
    { "Auto-start is off", "Início automático desligado" },
    { "Press Triangle to turn auto-start on. GoldHEN then starts Control4Free each time it loads.",
      "Aperte Triângulo para ligar o início automático. Aí o GoldHEN inicia o Control4Free toda vez que carrega." },
    { "Auto-start status unknown", "Estado do início automático desconhecido" },
    { "Checking with GoldHEN...", "Verificando com o GoldHEN..." },
    { "TV remote: OK opens actions / arrows select / Back returns", "Controle da TV: OK abre as ações / setas escolhem / Voltar retorna" },
    { "L1  Language: automatic", "L1  Idioma: automático" },
    { "L1  Language: English", "L1  Idioma: English" },
    { "L1  Language: Portuguese (Brazil)", "L1  Idioma: português (Brasil)" },
    { "Start", "Iniciar" },
    { "Set up", "Configurar" },
    { "Turn off auto-start", "Desligar início automático" },
    { "Update auto-start", "Atualizar início automático" },
    { "Turn on auto-start", "Ligar início automático" },
    { "Stop", "Parar" },
    { "Test controllers", "Testar controles" },
    { "Close", "Fechar" },

    /* Actions and stopping */
    { "Actions", "Ações" },
    { "Working... You can return to the screen.", "Trabalhando... Você pode voltar à tela." },
    { "Use arrows or the D-pad to choose an action.", "Use as setas ou o direcional para escolher uma ação." },
    { "Select", "Selecionar" },
    { "Back", "Voltar" },
    { "TV remote: OK select / Back return", "Controle da TV: OK seleciona / Voltar retorna" },
    { "Stop Control4Free?", "Parar o Control4Free?" },
    { "Every controller disconnects. You can start Control4Free again from this screen.",
      "Todos os controles serão desconectados. Você pode iniciar o Control4Free de novo nesta tela." },
    { "Cancel", "Cancelar" },
    { "Arrows / D-pad: select    OK / Cross: confirm    Back / Circle: cancel",
      "Setas / direcional: escolher    OK / X: confirmar    Voltar / Círculo: cancelar" },
    { "Resume", "Continuar" },
    { "Set up and start", "Configurar e iniciar" },
    { "Turn auto-start off", "Desligar o início automático" },
    { "Turn auto-start on", "Ligar o início automático" },
    { "Stop Control4Free", "Parar o Control4Free" },
    { "Close app", "Fechar o app" },
    { "Capture kernel log", "Capturar o log do kernel" },

    /* What the app last did */
    { "Checking Control4Free...", "Verificando o Control4Free..." },
    { "Could not initialize launcher. Close with PS and retry.", "Não foi possível iniciar o app. Feche com o botão PS e tente de novo." },
    { "Could not initialize input. Close with PS and retry.", "Não foi possível iniciar a entrada. Feche com o botão PS e tente de novo." },
    { "Setting up Control4Free. Please wait...", "Configurando o Control4Free. Aguarde..." },
    { "Starting Control4Free. Please wait...", "Iniciando o Control4Free. Aguarde..." },
    { "Stopping and releasing controllers...", "Parando e liberando os controles..." },
    { "Turning auto-start off...", "Desligando o início automático..." },
    { "Turning auto-start on...", "Ligando o início automático..." },
    { "Set up and running. GoldHEN will also start it after each restart.",
      "Configurado e rodando. O GoldHEN também vai iniciá-lo após cada reinício." },
    { "Ready. Open the address on your phone or PC.", "Pronto. Abra o endereço no seu celular ou PC." },
    { "Not running.", "Não está rodando." },
    { "The app cannot reach Control4Free: %s", "O app não consegue alcançar o Control4Free: %s" },
    { "No answer the app understands: %s", "Nenhuma resposta que o app entenda: %s" },
    { "Already running. Open the address on your phone or PC.", "Já está rodando. Abra o endereço no seu celular ou PC." },
    { "The app cannot check whether Control4Free is running (%s).", "O app não consegue verificar se o Control4Free está rodando (%s)." },
    { "Control4Free is not responding (%s). Wait a moment after waking. If it stays stuck, restart the PS4.",
      "O Control4Free não está respondendo (%s). Aguarde um pouco depois de sair do modo de repouso. Se continuar travado, reinicie o PS4." },
    { "The payload is missing. Reinstall the Control4Free package.", "O payload está faltando. Reinstale o pacote do Control4Free." },
    { "The bundled payload is damaged. Reinstall the Control4Free package.", "O payload incluído está danificado. Reinstale o pacote do Control4Free." },
    { "PayLoader did not answer (errno %d). Enable PayLoader Server in GoldHEN > Server Settings, then retry Start.",
      "O PayLoader não respondeu (errno %d). Ative o PayLoader Server em GoldHEN > Server Settings e tente Iniciar de novo." },
    { "The transfer to PayLoader broke off. Retry Start.", "A transferência para o PayLoader foi interrompida. Tente Iniciar de novo." },
    { "Sent, but Control4Free did not start (%s). Retry Start.", "Enviado, mas o Control4Free não iniciou (%s). Tente Iniciar de novo." },
    { "Stopped. Ready to start again.", "Parado. Pronto para iniciar de novo." },
    { "The app cannot reach Control4Free (%s). Stop it on its controller page.",
      "O app não consegue alcançar o Control4Free (%s). Pare-o pela página do controle." },
    { "This copy does not answer the app (%s). Stop it on its controller page.",
      "Esta cópia não responde ao app (%s). Pare-a pela página do controle." },
    { "Stop was not confirmed (%s). Check the controller page.", "A parada não foi confirmada (%s). Confira a página do controle." },
    { "Still stopping. Wait a moment, then try again.", "Ainda parando. Aguarde um pouco e tente de novo." },
    { "the status reply is not from Control4Free", "a resposta de status não é do Control4Free" },
    { "Auto-start is on. %s", "Início automático ligado. %s" },
    { "the bundled payload could not be read", "não foi possível ler o payload incluído" },
    { "GoldHEN did not let the app check (errno %d)", "O GoldHEN não deixou o app verificar (errno %d)" },
    { "Could not inspect AutoRun (errno %d)", "Não foi possível inspecionar o AutoRun (errno %d)" },
    { "Enabled, but the payload is missing; choose Update auto-start in Actions or press Triangle",
      "Ativado, mas o payload está faltando; escolha Atualizar o início automático em Ações ou aperte Triângulo" },
    { "Enabled, but the payload cannot be inspected (errno %d)", "Ativado, mas o payload não pode ser inspecionado (errno %d)" },
    { "The bundled payload could not be read. Reinstall the Control4Free package.",
      "Não foi possível ler o payload incluído. Reinstale o pacote do Control4Free." },
    { "GoldHEN did not let the app set up auto-start (errno %d).", "O GoldHEN não deixou o app configurar o início automático (errno %d)." },
    { "Could not copy Control4Free to /data/payloads (errno %d).", "Não foi possível copiar o Control4Free para /data/payloads (errno %d)." },
    { "Could not add Control4Free to GoldHEN's AutoRun list (errno %d).",
      "Não foi possível adicionar o Control4Free à lista de AutoRun do GoldHEN (errno %d)." },
    { "Auto-start is on. GoldHEN starts Control4Free each time it loads.",
      "Início automático ligado. O GoldHEN inicia o Control4Free toda vez que carrega." },
    { "GoldHEN did not let the app change auto-start (errno %d).", "O GoldHEN não deixou o app mudar o início automático (errno %d)." },
    { "Could not change GoldHEN's AutoRun list (errno %d).", "Não foi possível mudar a lista de AutoRun do GoldHEN (errno %d)." },
    { "Auto-start is off. Start Control4Free here with Cross, or from GoldHEN's Payloader LaunchPad.",
      "Início automático desligado. Inicie o Control4Free aqui com X, ou pelo Payloader LaunchPad do GoldHEN." },

    /* Controller test */
    { "Controller test", "Teste de controle" },
    { "See the controller input received by this PS4. Select a user, then press buttons or move the sticks. PS and Share go to the PS4 itself and aren't shown here.",
      "Veja a entrada de controle que este PS4 recebe. Escolha um usuário e aperte botões ou mova os analógicos. PS e Share vão para o próprio PS4 e não aparecem aqui." },
    { "Sticks", "Analógicos" },
    { "Left", "Esquerdo" },
    { "Right", "Direito" },
    { "Triggers", "Gatilhos" },
    { "Motion", "Movimento" },
    { "Acceleration (G)", "Aceleração (G)" },
    { "Gyro (rad/s)", "Giroscópio (rad/s)" },
    { "Orientation", "Orientação" },
    { "Light bar", "Barra de luz" },
    { "Hold L3 + R3: menu    TV remote: OK menu / Back leave", "Segure L3 + R3: menu    Controle da TV: OK menu / Voltar sai" },
    { "Waiting for the PS4's signed-in users...", "Aguardando os usuários conectados no PS4..." },
    { "No touch", "Sem toque" },
    { "Set by this test", "Definida por este teste" },
    { "As the PS4 sets it", "Como o PS4 define" },
    { "Controller page RTT", "RTT da página do controle" },
    { "No controller page for this user", "Nenhuma página de controle para este usuário" },
    { "C%d measuring...", "C%d medindo..." },
    { "User %d", "Usuário %d" },
    { "The PS4 refused that (0x%08x).", "O PS4 recusou isso (0x%08x)." },
    { "No other user is signed in.", "Nenhum outro usuário está conectado." },
    { "This user has no controller to test.", "Este usuário não tem controle para testar." },
    { "Large motor: rumbling for a second.", "Motor grande: vibrando por um segundo." },
    { "Small motor: rumbling for a second.", "Motor pequeno: vibrando por um segundo." },
    { "Light bar reset.", "Barra de luz restaurada." },
    { "Orientation reset.", "Orientação restaurada." },
    { "Light bar set. It goes back to normal when you leave.", "Barra de luz definida. Ela volta ao normal quando você sair." },
    { "This user's controller is unavailable.", "O controle deste usuário está indisponível." },
    { "Switch user", "Trocar usuário" },
    { "Rumble the large motor", "Vibrar o motor grande" },
    { "Rumble the small motor", "Vibrar o motor pequeno" },
    { "Choose the light-bar color", "Escolher a cor da barra de luz" },
    { "Reset the light bar", "Restaurar a barra de luz" },
    { "Reset orientation", "Restaurar orientação" },
    { "Back to the launcher", "Voltar ao app" },
    { "Return to live input", "Voltar à entrada ao vivo" },
    { "Choose another player", "Escolher outro jogador" },
    { "Recenter motion", "Recentralizar o movimento" },
    { "Choose any RGB color", "Escolher qualquer cor RGB" },
    { "One-second vibration", "Vibração de um segundo" },
    { "Restore default color", "Restaurar a cor padrão" },
    { "No other signed-in user", "Nenhum outro usuário conectado" },
    { "Controller unavailable", "Controle indisponível" },
    { "Large motor", "Motor grande" },
    { "Small motor", "Motor pequeno" },
    { "Reset light", "Restaurar luz" },
    { "Leave test", "Sair do teste" },
    { "Controller tools", "Ferramentas do controle" },
    { "Mix red, green and blue. Changes preview on the selected controller.",
      "Misture vermelho, verde e azul. As mudanças aparecem no controle selecionado." },
    { "Test motion, vibration and the light bar.", "Teste o movimento, a vibração e a barra de luz." },
    { "D-pad or arrows to move", "Direcional ou setas para mover" },
    { "Fast adjustment", "Ajuste rápido" },
    { "Fine adjustment", "Ajuste fino" },
    { "Done", "Concluído" },
    { "Up/Down: channel / Left/Right: adjust / OK: step size / Back: done",
      "Cima/Baixo: canal / Esquerda/Direita: ajustar / OK: passo / Voltar: concluir" },
    { "No one signed in", "Ninguém conectado" },
    { "Controller connected", "Controle conectado" },
    { "No controller connected", "Nenhum controle conectado" },
    { "Signed out", "Desconectado" },
    { "Cannot read (0x%08x)", "Não foi possível ler (0x%08x)" },
    { "Opening controller...", "Abrindo o controle..." },
    { "Waiting for a signed-in user", "Aguardando um usuário conectado" },
    { "SELECTED USER", "USUÁRIO SELECIONADO" },
    { "CONTROLLER", "CONTROLE" },
    { "TEST OUTPUT", "SAÍDA DO TESTE" },
    { "Light bar set by this test", "Barra de luz definida por este teste" },
    { "Light bar: PS4 default", "Barra de luz: padrão do PS4" },
    { "Large motor running", "Motor grande ligado" },
    { "Small motor running", "Motor pequeno ligado" },
    { "The light bar returns to its PS4 color when you leave the tester.", "A barra de luz volta à cor do PS4 quando você sai do teste." },
    { "Choose a tool, or resume testing to see live input.", "Escolha uma ferramenta, ou continue o teste para ver a entrada ao vivo." },
    { "Sign in another PS4 user to switch players here.", "Conecte outro usuário no PS4 para trocar de jogador aqui." },
    { "Connect this user's controller to use this tool.", "Conecte o controle deste usuário para usar esta ferramenta." },
    { "Red", "Vermelho" },
    { "Green", "Verde" },
    { "Blue", "Azul" },
    { "COLOR PREVIEW", "PRÉVIA DA COR" },
    { "Cross / OK switches fine and fast", "X / OK alterna entre fino e rápido" },
    { "Step: %d", "Passo: %d" },
    { "Reading this user's controller", "Lendo o controle deste usuário" },
    { "The PS4 would not read it (0x%08x)", "O PS4 não conseguiu ler (0x%08x)" },
    { "No one is signed in", "Ninguém está conectado" },
    { "Opening this user's controller...", "Abrindo o controle deste usuário..." },
    { "Rumbling: large motor", "Vibrando: motor grande" },
    { "Rumbling: small motor", "Vibrando: motor pequeno" },
    { "Hold L3 + R3 for two seconds for the menu: change user, rumble, light bar, leave.",
      "Segure L3 + R3 por dois segundos para o menu: trocar usuário, vibração, barra de luz, sair." },
};

enum { C4F_CAPTURES = 4, C4F_CAPTURE_SIZE = 192 };

/* The end of the '%' conversion that starts at `at`. */
static const char *c4fConversionEnd(const char *at)
{
    at++;
    while (*at && strchr("0123456789.-+ #lhz", *at)) at++;
    return *at ? at + 1 : at;
}

/* Whether `text` reads as `pattern`, each conversion standing for a non-empty
 * run of text, kept in `captures`. */
static int c4fMatch(const char *pattern, const char *text, char captures[][C4F_CAPTURE_SIZE], int n)
{
    while (*pattern && *pattern != '%') {
        if (*pattern++ != *text++) return 0;
    }
    if (!*pattern) return !*text;
    if (n == C4F_CAPTURES) return 0;
    const char *rest = c4fConversionEnd(pattern);
    for (size_t length = 1; text[length - 1]; length++) {
        if (length >= C4F_CAPTURE_SIZE) return 0;
        if (*rest && *rest != '%' && text[length] != *rest) continue;
        if (c4fMatch(rest, text + length, captures, n + 1)) {
            memcpy(captures[n], text, length);
            captures[n][length] = 0;
            return 1;
        }
    }
    return 0;
}

static const char *c4fExact(const char *text)
{
    for (size_t i = 0; i < sizeof(c4fPortuguese) / sizeof(c4fPortuguese[0]); i++)
        if (!strchr(c4fPortuguese[i].en, '%') && !strcmp(c4fPortuguese[i].en, text)) return c4fPortuguese[i].pt;
    return NULL;
}

const char *c4fPortugueseText(const char *text)
{
    /* Text and its width are asked for one after the other: a few results
     * stay valid at once. */
    static char shown[8][512];
    static int next;
    if (!text || !*text) return NULL;
    const char *exact = c4fExact(text);
    if (exact) return exact;
    char captures[C4F_CAPTURES][C4F_CAPTURE_SIZE];
    for (size_t i = 0; i < sizeof(c4fPortuguese) / sizeof(c4fPortuguese[0]); i++) {
        const C4fPhrase *p = &c4fPortuguese[i];
        if (!strchr(p->en, '%') || !c4fMatch(p->en, text, captures, 0)) continue;
        char *out = shown[next];
        next = (next + 1) % 8;
        size_t used = 0;
        int n = 0;
        for (const char *at = p->pt; *at && used + 1 < sizeof(shown[0]);) {
            if (*at != '%') { out[used++] = *at++; continue; }
            const char *end = c4fConversionEnd(at);
            /* A filled-in text may itself be one the app translates. */
            const char *value = n < C4F_CAPTURES ? captures[n] : "";
            const char *translated = end[-1] == 's' ? c4fExact(value) : NULL;
            used += (size_t)snprintf(out + used, sizeof(shown[0]) - used, "%s", translated ? translated : value);
            if (used >= sizeof(shown[0])) used = sizeof(shown[0]) - 1;
            n++;
            at = end;
        }
        out[used] = 0;
        return out;
    }
    return NULL;
}
