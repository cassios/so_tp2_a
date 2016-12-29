# PAGINADOR DE MEMÓRIA - RELATÓRIO

1. Termo de compromisso

Os membros do grupo afirmam que todo o código desenvolvido para este
trabalho é de autoria própria.  Exceto pelo material listado no item
3 deste relatório, os membros do grupo afirmam não ter copiado
material da Internet nem ter obtido código de terceiros.

2. Membros do grupo e alocação de esforço

Preencha as linhas abaixo com o nome e o e-mail dos integrantes do
grupo.  Substitua marcadores `XX` pela contribuição de cada membro
do grupo no desenvolvimento do trabalho (os valores devem somar
100%).

  * Cassios Marques <cassios.kmm@gmail.com> 50%
  * Yuri <ignitzhjfk@gmail.com> 50%

3. Referências bibliográficas
    Foi reutilizado a estrutura de dados de lista encadeada
    disponibilizada juntamente com o trabalho 1b.

4. Estruturas de dados

  1. Descreva e justifique as estruturas de dados utilizadas em sua solução.
    As principais estruturas de dados usadas foram:
    a) FrameTable que contém o numero de frames disponíveis, o tamanho da página, o último índice 
        visitado pelo algoritmo de segunda chance e um array de FrameNodes. Cada FrameNode armazena
        os dados de cada frame (pid, pagina e se esta foi acessada)
    b) BlockTable que contém o número de blocos disponíveis e um array de BlockNodes. Cada BlockNode
        armazena um ponteiro para a página a que o bloco foi destinado e uma flag indicando se esta página
        já foi mandada para o disco.
    c) PageTable armazena o pid de um processo juntamente com sua lista de páginas.
        Foi utilidado uma lista encadeada para armazenar as paginas, pois não é possível
        saber a priori quantas páginas serão requeridas por um processo.
        Cada pagina(Page) contém o numero do seu bloco e frame, caso esta esteja em memoria principal,
        o seu endereço virtual e uma flag indicando se houveram escritas a pagina.

  2. Descreva o mecanismo utilizado para controle de acesso
     e modificação às páginas.
