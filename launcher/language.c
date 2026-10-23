/* Offline interface catalogs. Source message keys are looked up exactly,
 * before values are inserted, so user data is never translated. */
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
#define C4F_SYSTEM_LANG_ARABIC 21

#define C4F_LANGUAGE_DIR "/data/control4free"
#define C4F_LANGUAGE_FILE C4F_LANGUAGE_DIR "/launcher-language"

static const char *const c4fLanguageNames[C4F_LANGUAGE_COUNT] = { "auto", "en", "pt-BR", "ar" };
static int c4fActiveLanguage = C4F_LANGUAGE_ENGLISH;

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

static int c4fSystemLanguage(void)
{
    int language = -1;
    if (sceSystemServiceParamGetInt(C4F_SYSTEM_PARAM_LANG, &language) < 0)
        return C4F_LANGUAGE_ENGLISH;
    if (language == C4F_SYSTEM_LANG_ARABIC) return C4F_LANGUAGE_ARABIC;
    if (language == C4F_SYSTEM_LANG_PORTUGUESE || language == C4F_SYSTEM_LANG_PORTUGUESE_BR)
        return C4F_LANGUAGE_PORTUGUESE;
    return C4F_LANGUAGE_ENGLISH;
}

void c4fLanguageUse(int choice)
{
    static int used = -1;
    int selected = choice == C4F_LANGUAGE_AUTO ? c4fSystemLanguage() : choice;
    if (selected <= C4F_LANGUAGE_AUTO || selected >= C4F_LANGUAGE_COUNT)
        selected = C4F_LANGUAGE_ENGLISH;
    if (selected == used) return;
    used = selected;
    c4fActiveLanguage = selected;
    c4fSetTextTranslation(selected == C4F_LANGUAGE_PORTUGUESE ? c4fPortugueseText :
                          selected == C4F_LANGUAGE_ARABIC ? c4fArabicText : NULL);
    c4fSetTextDirection(selected == C4F_LANGUAGE_ARABIC);
}

/* ---- the translations ---- */

/* Format conversions remain in the same order; callers format translated keys. */
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

/* Exact interface keys only. Values are formatted after lookup by the caller. */
static const C4fPhrase c4fArabic[] = {
    { "Touchpad", "لوحة اللمس" },
    { "Not responding", "لا يستجيب" },
    { "Check Control4Free", "تحقق من Control4Free" },
    { "Something answers on port 4264 but not the way this app expects. If an older Control4Free is running, stop it on its controller page or restart the PS4.", "توجد استجابة على المنفذ 4264 لا يتعرف عليها التطبيق. إذا كان إصدار أقدم من Control4Free يعمل، فأوقفه من صفحة التحكم أو أعد تشغيل PS4." },
    { "App error", "خطأ في التطبيق" },
    { "Reopen the app", "أعد فتح التطبيق" },
    { "The app could not start properly. Close it with the PS button and open it again. Control4Free itself is not affected.", "تعذر بدء التطبيق. أغلقه باستخدام زر PS ثم افتحه مجددًا. لا يؤثر ذلك على خدمة Control4Free." },
    { "Working", "جارٍ التنفيذ" },
    { "Please wait", "يرجى الانتظار" },
    { "Checking", "جارٍ التحقق" },
    { "1 controller connected", "وحدة تحكم واحدة متصلة" },
    { "%d controllers connected", "وحدات تحكم متصلة: %d" },
    { "Running", "يعمل" },
    { "Control4Free is running", "Control4Free يعمل" },
    { "Open the address or scan the code on your phone or PC. Pick a controller there, then choose its user on the PS4.", "افتح العنوان أو امسح الرمز بهاتفك أو حاسوبك. اختر وحدة تحكم، ثم اختر مستخدمها على PS4." },
    { "Stopped", "متوقف" },
    { "Start Control4Free", "تشغيل Control4Free" },
    { "It starts with GoldHEN after each restart. To start now, enable GoldHEN's PayLoader Server and press Cross.", "يبدأ مع GoldHEN بعد كل إعادة تشغيل. للبدء الآن، فعّل PayLoader Server في GoldHEN واضغط ×." },
    { "Not running", "لا يعمل" },
    { "Set up Control4Free", "إعداد Control4Free" },
    { "Press Cross to set up auto-start. To start Control4Free now, enable GoldHEN's PayLoader Server.", "اضغط × لإعداد التشغيل التلقائي. لتشغيل Control4Free الآن، فعّل PayLoader Server في GoldHEN." },
    { "Open on your phone or PC", "افتحه على هاتفك أو حاسوبك" },
    { "Connect the PS4 to your home network", "صِل PS4 بشبكتك المنزلية" },
    { "Use the same network as the PS4. Touch, a keyboard and gamepads connected to the phone or PC all work.", "استخدم شبكة PS4 نفسها. يمكنك اللعب باللمس أو بلوحة مفاتيح أو بوحدة تحكم متصلة بهاتفك أو حاسوبك." },
    { "Working...", "جارٍ التنفيذ..." },
    { "Scan with your phone's camera", "امسح الرمز بكاميرا هاتفك" },
    { "The code appears once it runs", "يظهر الرمز عند تشغيل الخدمة" },
    { "Installed %s; running %s", "المثبّت %s؛ قيد التشغيل %s" },
    { "Press Square, then Cross to stop, and Cross again to start this app's version. All players disconnect.", "اضغط المربع ثم × للإيقاف، ثم × مجددًا لتشغيل إصدار هذا التطبيق. سينقطع اتصال جميع اللاعبين." },
    { "Starts by itself with GoldHEN", "يبدأ تلقائيًا مع GoldHEN" },
    { "GoldHEN starts Control4Free each time it loads. Closing this app leaves your controllers running. Turn off other controller plugins in your games.", "يبدأ GoldHEN خدمة Control4Free عند تحميله. تبقى وحدات التحكم متصلة بعد إغلاق التطبيق. عطّل إضافات التحكم الأخرى في ألعابك." },
    { "Auto-start uses an older Control4Free", "التشغيل التلقائي يستخدم إصدارًا أقدم" },
    { "Press Triangle to update auto-start. GoldHEN uses this version after the next restart.", "اضغط المثلث لتحديث التشغيل التلقائي. سيستخدم GoldHEN هذا الإصدار بعد إعادة التشغيل التالية." },
    { "Auto-start is off", "التشغيل التلقائي معطّل" },
    { "Press Triangle to turn auto-start on. GoldHEN then starts Control4Free each time it loads.", "اضغط المثلث لتفعيل التشغيل التلقائي. سيبدأ GoldHEN خدمة Control4Free عند تحميله." },
    { "Auto-start status unknown", "حالة التشغيل التلقائي غير معروفة" },
    { "Checking with GoldHEN...", "جارٍ التحقق من GoldHEN..." },
    { "TV remote: OK opens actions / arrows select / Back returns", "ريموت التلفاز: OK للإجراءات / الأسهم للاختيار / رجوع للعودة" },
    { "L1  Language: automatic", "L1  اللغة: تلقائي" },
    { "L1  Language: English", "L1  اللغة: English" },
    { "L1  Language: Portuguese (Brazil)", "L1  اللغة: البرتغالية (البرازيل)" },
    { "Start", "تشغيل" },
    { "Set up", "إعداد" },
    { "Turn off auto-start", "تعطيل التشغيل التلقائي" },
    { "Update auto-start", "تحديث التشغيل التلقائي" },
    { "Turn on auto-start", "تفعيل التشغيل التلقائي" },
    { "Stop", "إيقاف" },
    { "Test controllers", "اختبار وحدات التحكم" },
    { "Close", "إغلاق" },
    { "Actions", "الإجراءات" },
    { "Working... You can return to the screen.", "جارٍ التنفيذ... يمكنك العودة إلى الشاشة." },
    { "Use arrows or the D-pad to choose an action.", "استخدم الأسهم أو أزرار الاتجاهات لاختيار إجراء." },
    { "Select", "اختيار" },
    { "Back", "رجوع" },
    { "TV remote: OK select / Back return", "ريموت التلفاز: OK للاختيار / رجوع للعودة" },
    { "Stop Control4Free?", "إيقاف Control4Free؟" },
    { "Every controller disconnects. You can start Control4Free again from this screen.", "سينقطع اتصال جميع وحدات التحكم. يمكنك تشغيل Control4Free مجددًا من هذه الشاشة." },
    { "Cancel", "إلغاء" },
    { "Arrows / D-pad: select    OK / Cross: confirm    Back / Circle: cancel", "الأسهم / الاتجاهات: اختيار    OK / ×: تأكيد    رجوع / الدائرة: إلغاء" },
    { "Resume", "متابعة" },
    { "Set up and start", "إعداد وتشغيل" },
    { "Turn auto-start off", "تعطيل التشغيل التلقائي" },
    { "Turn auto-start on", "تفعيل التشغيل التلقائي" },
    { "Stop Control4Free", "إيقاف Control4Free" },
    { "Close app", "إغلاق التطبيق" },
    { "Capture kernel log", "تسجيل سجل النواة" },
    { "Checking Control4Free...", "جارٍ التحقق من Control4Free..." },
    { "Could not initialize launcher. Close with PS and retry.", "تعذر بدء التطبيق. أغلقه بزر PS وحاول مجددًا." },
    { "Could not initialize input. Close with PS and retry.", "تعذر بدء الإدخال. أغلق التطبيق بزر PS وحاول مجددًا." },
    { "Setting up Control4Free. Please wait...", "جارٍ إعداد Control4Free. يرجى الانتظار..." },
    { "Starting Control4Free. Please wait...", "جارٍ تشغيل Control4Free. يرجى الانتظار..." },
    { "Stopping and releasing controllers...", "جارٍ الإيقاف وفصل وحدات التحكم..." },
    { "Turning auto-start off...", "جارٍ تعطيل التشغيل التلقائي..." },
    { "Turning auto-start on...", "جارٍ تفعيل التشغيل التلقائي..." },
    { "Set up and running. GoldHEN will also start it after each restart.", "اكتمل الإعداد والخدمة تعمل. سيشغلها GoldHEN أيضًا بعد كل إعادة تشغيل." },
    { "Ready. Open the address on your phone or PC.", "جاهز. افتح العنوان على هاتفك أو حاسوبك." },
    { "Not running.", "لا يعمل." },
    { "The app cannot reach Control4Free: %s", "يتعذر اتصال التطبيق بخدمة Control4Free: %s" },
    { "No answer the app understands: %s", "لم يتعرف التطبيق على الاستجابة: %s" },
    { "Already running. Open the address on your phone or PC.", "الخدمة تعمل بالفعل. افتح العنوان على هاتفك أو حاسوبك." },
    { "The app cannot check whether Control4Free is running (%s).", "تعذر على التطبيق التحقق من تشغيل Control4Free (%s)." },
    { "Control4Free is not responding (%s). Wait a moment after waking. If it stays stuck, restart the PS4.", "Control4Free لا يستجيب (%s). انتظر قليلًا بعد الخروج من وضع السكون. إذا استمرت المشكلة، فأعد تشغيل PS4." },
    { "The payload is missing. Reinstall the Control4Free package.", "ملف الخدمة مفقود. أعد تثبيت حزمة Control4Free." },
    { "The bundled payload is damaged. Reinstall the Control4Free package.", "ملف الخدمة المرفق تالف. أعد تثبيت حزمة Control4Free." },
    { "PayLoader did not answer (errno %d). Enable PayLoader Server in GoldHEN > Server Settings, then retry Start.", "لم يستجب PayLoader (errno %d). فعّل PayLoader Server من GoldHEN > Server Settings، ثم حاول التشغيل مجددًا." },
    { "The transfer to PayLoader broke off. Retry Start.", "انقطع الإرسال إلى PayLoader. حاول التشغيل مجددًا." },
    { "Sent, but Control4Free did not start (%s). Retry Start.", "اكتمل الإرسال، لكن Control4Free لم يبدأ (%s). حاول التشغيل مجددًا." },
    { "Stopped. Ready to start again.", "توقفت الخدمة. جاهزة للتشغيل مجددًا." },
    { "The app cannot reach Control4Free (%s). Stop it on its controller page.", "يتعذر اتصال التطبيق بخدمة Control4Free (%s). أوقفها من صفحة التحكم." },
    { "This copy does not answer the app (%s). Stop it on its controller page.", "هذه النسخة لا تستجيب للتطبيق (%s). أوقفها من صفحة التحكم." },
    { "Stop was not confirmed (%s). Check the controller page.", "لم يتأكد الإيقاف (%s). تحقق من صفحة التحكم." },
    { "Still stopping. Wait a moment, then try again.", "لا تزال الخدمة تتوقف. انتظر قليلًا وحاول مجددًا." },
    { "the status reply is not from Control4Free", "استجابة الحالة ليست من Control4Free" },
    { "Auto-start is on. %s", "التشغيل التلقائي مفعّل. %s" },
    { "the bundled payload could not be read", "تعذرت قراءة ملف الخدمة المرفق" },
    { "GoldHEN did not let the app check (errno %d)", "لم يسمح GoldHEN للتطبيق بالتحقق (errno %d)" },
    { "Could not inspect AutoRun (errno %d)", "تعذر فحص AutoRun (errno %d)" },
    { "Enabled, but the payload is missing; choose Update auto-start in Actions or press Triangle", "مفعّل، لكن ملف الخدمة مفقود؛ اختر تحديث التشغيل التلقائي من الإجراءات أو اضغط المثلث" },
    { "Enabled, but the payload cannot be inspected (errno %d)", "مفعّل، لكن تعذر فحص ملف الخدمة (errno %d)" },
    { "The bundled payload could not be read. Reinstall the Control4Free package.", "تعذرت قراءة ملف الخدمة المرفق. أعد تثبيت حزمة Control4Free." },
    { "GoldHEN did not let the app set up auto-start (errno %d).", "لم يسمح GoldHEN بإعداد التشغيل التلقائي (errno %d)." },
    { "Could not copy Control4Free to /data/payloads (errno %d).", "تعذر نسخ Control4Free إلى /data/payloads (errno %d)." },
    { "Could not add Control4Free to GoldHEN's AutoRun list (errno %d).", "تعذرت إضافة Control4Free إلى قائمة AutoRun في GoldHEN (errno %d)." },
    { "Auto-start is on. GoldHEN starts Control4Free each time it loads.", "التشغيل التلقائي مفعّل. يبدأ GoldHEN خدمة Control4Free عند تحميله." },
    { "GoldHEN did not let the app change auto-start (errno %d).", "لم يسمح GoldHEN بتغيير التشغيل التلقائي (errno %d)." },
    { "Could not change GoldHEN's AutoRun list (errno %d).", "تعذر تعديل قائمة AutoRun في GoldHEN (errno %d)." },
    { "Auto-start is off. Start Control4Free here with Cross, or from GoldHEN's Payloader LaunchPad.", "التشغيل التلقائي معطّل. شغّل Control4Free هنا بزر × أو من Payloader LaunchPad في GoldHEN." },
    { "Controller test", "اختبار وحدة التحكم" },
    { "See the controller input received by this PS4. Select a user, then press buttons or move the sticks. PS and Share go to the PS4 itself and aren't shown here.", "شاهد الإدخال الذي يتلقاه PS4. اختر مستخدمًا ثم اضغط الأزرار أو حرّك العصوين. يذهب زرا PS وShare مباشرة إلى PS4 ولا يظهران هنا." },
    { "Sticks", "العصوان" },
    { "Left", "اليسرى" },
    { "Right", "اليمنى" },
    { "Triggers", "الزنادان" },
    { "Motion", "الحركة" },
    { "Acceleration (G)", "التسارع (G)" },
    { "Gyro (rad/s)", "الجيروسكوب (rad/s)" },
    { "Orientation", "الاتجاه" },
    { "Light bar", "شريط الإضاءة" },
    { "Hold L3 + R3: menu    TV remote: OK menu / Back leave", "اضغط مطولًا L3 + R3: القائمة    ريموت التلفاز: OK للقائمة / رجوع للخروج" },
    { "Waiting for the PS4's signed-in users...", "بانتظار المستخدمين المسجّلين على PS4..." },
    { "No touch", "لا يوجد لمس" },
    { "Set by this test", "مضبوط من هذا الاختبار" },
    { "As the PS4 sets it", "حسب إعداد PS4" },
    { "Controller page RTT", "زمن اتصال صفحة التحكم" },
    { "No controller page for this user", "لا توجد صفحة تحكم لهذا المستخدم" },
    { "C%d measuring...", "C%d جارٍ القياس..." },
    { "User %d", "المستخدم %d" },
    { "The PS4 refused that (0x%08x).", "رفض PS4 هذا الإجراء (0x%08x)." },
    { "No other user is signed in.", "لا يوجد مستخدم آخر مسجّل." },
    { "This user has no controller to test.", "لا توجد وحدة تحكم لهذا المستخدم لاختبارها." },
    { "Large motor: rumbling for a second.", "المحرك الكبير: اهتزاز لمدة ثانية." },
    { "Small motor: rumbling for a second.", "المحرك الصغير: اهتزاز لمدة ثانية." },
    { "Light bar reset.", "أُعيد ضبط شريط الإضاءة." },
    { "Orientation reset.", "أُعيد ضبط الاتجاه." },
    { "Light bar set. It goes back to normal when you leave.", "ضُبط شريط الإضاءة. سيعود إلى لونه المعتاد عند الخروج." },
    { "This user's controller is unavailable.", "وحدة تحكم هذا المستخدم غير متاحة." },
    { "Switch user", "تبديل المستخدم" },
    { "Rumble the large motor", "تشغيل اهتزاز المحرك الكبير" },
    { "Rumble the small motor", "تشغيل اهتزاز المحرك الصغير" },
    { "Choose the light-bar color", "اختيار لون شريط الإضاءة" },
    { "Reset the light bar", "إعادة ضبط شريط الإضاءة" },
    { "Reset orientation", "إعادة ضبط الاتجاه" },
    { "Back to the launcher", "العودة إلى المشغّل" },
    { "Return to live input", "العودة إلى الإدخال المباشر" },
    { "Choose another player", "اختيار لاعب آخر" },
    { "Recenter motion", "إعادة توسيط الحركة" },
    { "Choose any RGB color", "اختيار أي لون RGB" },
    { "One-second vibration", "اهتزاز لمدة ثانية" },
    { "Restore default color", "استعادة اللون الافتراضي" },
    { "No other signed-in user", "لا يوجد مستخدم آخر مسجّل" },
    { "Controller unavailable", "وحدة التحكم غير متاحة" },
    { "Large motor", "المحرك الكبير" },
    { "Small motor", "المحرك الصغير" },
    { "Reset light", "إعادة ضبط اللون" },
    { "Leave test", "مغادرة الاختبار" },
    { "Controller tools", "أدوات وحدة التحكم" },
    { "Mix red, green and blue. Changes preview on the selected controller.", "امزج الأحمر والأخضر والأزرق. يظهر اللون على وحدة التحكم المختارة." },
    { "Test motion, vibration and the light bar.", "اختبر الحركة والاهتزاز وشريط الإضاءة." },
    { "D-pad or arrows to move", "استخدم الاتجاهات أو الأسهم للتنقل" },
    { "Fast adjustment", "تعديل سريع" },
    { "Fine adjustment", "تعديل دقيق" },
    { "Done", "تم" },
    { "Up/Down: channel / Left/Right: adjust / OK: step size / Back: done", "أعلى/أسفل: القناة / يمين/يسار: تعديل / OK: مقدار التغيير / رجوع: إنهاء" },
    { "No one signed in", "لا يوجد مستخدم مسجّل" },
    { "Controller connected", "وحدة التحكم متصلة" },
    { "No controller connected", "لا توجد وحدة تحكم متصلة" },
    { "Signed out", "سُجّل الخروج" },
    { "Cannot read (0x%08x)", "تعذرت القراءة (0x%08x)" },
    { "Opening controller...", "جارٍ فتح وحدة التحكم..." },
    { "Waiting for a signed-in user", "بانتظار مستخدم مسجّل" },
    { "SELECTED USER", "المستخدم المختار" },
    { "CONTROLLER", "وحدة التحكم" },
    { "TEST OUTPUT", "خرج الاختبار" },
    { "Light bar set by this test", "شريط الإضاءة مضبوط من الاختبار" },
    { "Light bar: PS4 default", "شريط الإضاءة: إعداد PS4 الافتراضي" },
    { "Large motor running", "المحرك الكبير يعمل" },
    { "Small motor running", "المحرك الصغير يعمل" },
    { "The light bar returns to its PS4 color when you leave the tester.", "يعود شريط الإضاءة إلى لون PS4 عند مغادرة الاختبار." },
    { "Choose a tool, or resume testing to see live input.", "اختر أداة أو تابع الاختبار لمشاهدة الإدخال المباشر." },
    { "Sign in another PS4 user to switch players here.", "سجّل دخول مستخدم آخر على PS4 للتبديل بين اللاعبين هنا." },
    { "Connect this user's controller to use this tool.", "صِل وحدة تحكم هذا المستخدم لاستخدام الأداة." },
    { "Red", "أحمر" },
    { "Green", "أخضر" },
    { "Blue", "أزرق" },
    { "COLOR PREVIEW", "معاينة اللون" },
    { "Cross / OK switches fine and fast", "× / OK للتبديل بين التعديل الدقيق والسريع" },
    { "Step: %d", "مقدار التغيير: %d" },
    { "Reading this user's controller", "جارٍ قراءة وحدة تحكم هذا المستخدم" },
    { "The PS4 would not read it (0x%08x)", "لم يتمكن PS4 من قراءتها (0x%08x)" },
    { "No one is signed in", "لا يوجد مستخدم مسجّل" },
    { "Opening this user's controller...", "جارٍ فتح وحدة تحكم هذا المستخدم..." },
    { "Rumbling: large motor", "اهتزاز: المحرك الكبير" },
    { "Rumbling: small motor", "اهتزاز: المحرك الصغير" },
    { "Hold L3 + R3 for two seconds for the menu: change user, rumble, light bar, leave.", "اضغط مطولًا L3 + R3 لمدة ثانيتين لفتح القائمة: المستخدم والاهتزاز والإضاءة والخروج." },
    { "L1  Language: Arabic", "L1  اللغة: العربية" },
};

static const char *c4fFind(const C4fPhrase *phrases, size_t count, const char *key)
{
    if (!key) return NULL;
    for (size_t i = 0; i < count; ++i)
        if (!strcmp(phrases[i].en, key)) return phrases[i].pt;
    return NULL;
}

const char *c4fPortugueseText(const char *key)
{
    if (key && !strcmp(key, "L1  Language: Arabic")) return "L1  Idioma: العربية";
    return c4fFind(c4fPortuguese, sizeof(c4fPortuguese) / sizeof(c4fPortuguese[0]), key);
}

const char *c4fArabicText(const char *key)
{
    return c4fFind(c4fArabic, sizeof(c4fArabic) / sizeof(c4fArabic[0]), key);
}

const char *c4fTr(const char *key)
{
    const char *translated = c4fActiveLanguage == C4F_LANGUAGE_PORTUGUESE ? c4fPortugueseText(key) :
                             c4fActiveLanguage == C4F_LANGUAGE_ARABIC ? c4fArabicText(key) : NULL;
    return translated ? translated : key;
}

int c4fLanguageIsArabic(void)
{
    return c4fActiveLanguage == C4F_LANGUAGE_ARABIC;
}
