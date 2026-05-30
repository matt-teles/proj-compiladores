# Testes manuais

Os arquivos desta pasta exercitam recursos relevantes do compilador sem
utilizar uma suíte automatizada. Primeiro compile o projeto na pasta raiz:

```bash
make
```

Depois execute individualmente os comandos abaixo.

## Programas válidos

### 1. Programa mínimo

```bash
./build/SAL exemplos/01_minimo.sal
```

Ao executar o MEPA gerado, a saída esperada é:

```text
42
```

### 2. Função `quadrado`

```bash
./build/SAL exemplos/02_quadrado.sal --tokens --symtab --trace
```

O programa declara uma função com parâmetro e retorno inteiro. Saída esperada:

```text
49
```

As opções adicionais também criam:

```text
resultados/02_quadrado.tk
resultados/02_quadrado.ts
resultados/02_quadrado.trc
```

Todos os artefatos produzidos ficam separados dos fontes desta pasta.

### 3. Controle de fluxo

```bash
./build/SAL exemplos/03_controle_fluxo.sal
```

O programa combina variável global, `for`, bloco interno, `if` e `match`.
Saída esperada:

```text
1
3
5
100
2
```

### 4. Função recursiva

```bash
./build/SAL exemplos/04_fatorial_recursivo.sal
```

O programa calcula `5!`. Saída esperada:

```text
120
```

### 5. Entrada e saída

```bash
./build/SAL exemplos/05_entrada_saida.sal
```

Ao executar o MEPA, informe o valor contido em `entrada_05.txt`:

```text
8
```

Saída esperada:

```text
64
```

### 10. Passo armazenado em variável

```bash
./build/SAL exemplos/10_step_variavel.sal
```

Saída esperada:

```text
1
3
5
```

## Programas inválidos

Os exemplos abaixo devem encerrar a compilação com erro e não produzir um
novo arquivo `.mepa` válido.

### 6. Vetor proibido

```bash
./build/SAL exemplos/06_erro_lexico_vetor.sal
```

Mensagem esperada:

```text
[ERRO LEXICO] linha 4: vetores nao sao suportados nesta versao da SAL
```

### 7. Ponto e vírgula ausente

```bash
./build/SAL exemplos/07_erro_sintatico.sal
```

Mensagem esperada:

```text
[ERRO SINTATICO] linha 7: esperado sPONTVIRG, encontrado sPRINT ('print')
```

### 8. Atribuição de tipo incompatível

```bash
./build/SAL exemplos/08_erro_semantico.sal
```

Mensagem esperada:

```text
[ERRO SEMANTICO] linha 6: atribuicao: esperado tipo bool, encontrado int
```

### 9. Duas falhas em linhas diferentes

```bash
./build/SAL exemplos/09_duas_falhas.sal
```

O arquivo possui `$` na linha 4 e um acesso indexado na linha 12. A análise é
interrompida no primeiro erro, como definido para o compilador:

```text
[ERRO LEXICO] linha 4: caractere invalido '$'
```

Para observar a segunda falha, substitua temporariamente `$` por `*` e compile
novamente. O resultado será:

```text
[ERRO LEXICO] linha 12: vetores nao sao suportados nesta versao da SAL
```
