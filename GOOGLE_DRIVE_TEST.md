# JellyDrive5 — login direto do Google Drive

Esta versão experimental do JellyDrive5 abre diretamente no navegador de arquivos do Google Drive no PS5. Não exige Jellyfin, Emby, rclone.conf nem credenciais OAuth configuradas manualmente no pendrive.

## Entrar com sua conta Google

1. Execute o JellyDrive5 no PS5 com o jailbreak e ShadowMount+ ativos.
2. Na tela inicial, pressione X para gerar um código de autorização.
3. No celular conectado à mesma rede de internet do PS5, abra o endereço mostrado na tela do console. Você também pode digitar o código no site do serviço.
4. Entre na conta Google e autorize a permissão de leitura solicitada.
5. Deixe o JellyDrive5 aberto. Ele verifica a autorização automaticamente e abre a lista de Drives compartilhados quando o processo terminar.
6. Selecione seu Drive de equipe, navegue nas pastas e pressione X em um vídeo para reproduzir.

Se o código expirar, pressione X novamente para gerar outro. Se o site informar que o dispositivo e o celular precisam estar na mesma rede, certifique-se de que o celular está no Wi-Fi da mesma conexão do PS5, e não usando dados móveis ou uma VPN.

## Como o login funciona

O fluxo de autorização por código usa o serviço público e de código aberto do projeto cguZZman: https://drive.signinserver.com. O aplicativo envia o pedido de login ao serviço, você autoriza no navegador do celular e o console recebe os tokens necessários para chamar diretamente a API do Google Drive.

O serviço de login intermedeia a autorização OAuth e também a renovação de tokens. O conteúdo dos arquivos e o fluxo dos vídeos não passam pelo serviço de login: a reprodução é feita diretamente entre o PS5 e os servidores do Google Drive. Como parte desse desenho, o serviço recebe temporariamente os tokens na etapa de autorização e recebe o refresh token quando o aplicativo pede a renovação. Consulte o código e a política do serviço antes de autorizar se quiser avaliar essa relação de confiança.

O serviço solicita a permissão de leitura do Google Drive. O Google pode exibir uma tela de autorização ou aviso de aplicativo não verificado; confira o nome e as permissões apresentados antes de aceitar. O serviço atual é administrado por um terceiro. Para evitar essa dependência, uma alternativa é hospedar uma instância própria do serviço ou voltar ao método legado com rclone.

- Código-fonte do serviço: https://github.com/cguZZman/signin-server
- Documentação do serviço: https://github.com/cguZZman/signin-server/blob/main/SETUP.md
- Projeto JellyDrive5: https://github.com/tales92/Jelly5-Gdrive/tree/feature/google-drive-direct

## O que esta compilação oferece

- Login pelo console com código de autorização e aprovação no celular.
- Salvamento local da sessão para abrir o Drive sem repetir o login em cada inicialização.
- Renovação de tokens através do serviço de login.
- Navegação por Drives compartilhados, pastas e arquivos de vídeo reconhecidos.
- Reprodução com o player nativo do Jelly5 usando um cabeçalho de autorização.
- Identificador separado PPSA99508 para não substituir o Jelly5 original.

## Limitações de teste

Esta é uma versão experimental. O sucesso da compilação verifica apenas que o aplicativo foi compilado e empacotado; o login real, a renovação de tokens, a listagem de Drives, os redirecionamentos do Google, a busca dentro do vídeo e os codecs precisam ser testados no PS5 físico.

A biblioteca ainda não exibe pôsteres ou metadados TMDB enriquecidos. O aplicativo é somente leitura e não envia, altera nem apaga arquivos do Drive.
