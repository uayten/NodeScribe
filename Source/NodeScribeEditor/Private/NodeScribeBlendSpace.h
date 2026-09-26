#pragma once

#include "CoreMinimal.h"

class UBlendSpace;

/**
 * Preencher um BlendSpace por texto.
 *
 * Existe porque a ficha nao alcanca. `SampleData` e' array de struct e
 * `BlendParameters` e' array fixo de struct: escrever nos dois pela ficha seria
 * montar a mao o que a Engine ja' monta -- e sem a validacao dela, que e' quem
 * recalcula a malha de interpolacao. Sem essa malha o BlendSpace existe, abre,
 * e nao interpola nada.
 *
 * Uma linha por sample, no mesmo espirito do resto do plugin:
 *
 *     eixo X : Speed = 0 .. 600
 *     MM_Idle = 0
 *     MF_Unarmed_Walk_Fwd = 300
 *     MF_Unarmed_Jog_Fwd = 600
 *
 * Em duas dimensoes o eixo Y entra igual, e o sample ganha a segunda posicao:
 *
 *     eixo X : Direction = -180 .. 180
 *     eixo Y : Speed = 0 .. 600
 *     MM_Idle = 0, 0
 *     MF_Unarmed_Walk_Fwd = 0, 300
 *
 * O nome do asset segue a mesma regra do AnimGraph: nome curto se for unico,
 * caminho completo se nao for. Dois assets com o mesmo nome curto nao viram
 * escolha -- sai a lista, e quem decide e' quem escreveu.
 */
namespace NodeScribeBlendSpace
{

struct FResult
{
	int32 SamplesAdded = 0;
	int32 AxesSet = 0;
	TArray<FString> Diagnostics;
};

/**
 * Aplica o texto no BlendSpace.
 *
 * Os eixos sao aplicados antes dos samples, aconteca em que ordem aparecerem no
 * texto: um sample fora do intervalo do eixo e' recusado pela Engine, e definir
 * o intervalo depois nao o traz de volta.
 *
 * Nao apaga o que ja' esta' la'. Como toda escrita do plugin, o texto e' uma
 * lista de mudancas -- para recomecar do zero, crie outro asset.
 */
FResult Write(UBlendSpace* BlendSpace, const FString& Text);

/**
 * O caminho de volta: os eixos e os samples, no formato que Write aceita.
 *
 * Vive aqui, e nao numa ferramenta propria, porque a pergunta e' a mesma que a
 * ficha responde -- `read_object` num BlendSpace chama isto e emenda o bloco no
 * fim. Antes, a ficha dizia `nao sei escrever o valor de: Sample Data`, e nao
 * havia como saber o que ja' estava no asset sem abrir o editor. Escrever as
 * cegas num BlendSpace que ja' tem samples e' como o resto do plugin trata
 * qualquer escrita sem leitura: adivinhacao.
 *
 * Vazio quando o BlendSpace nao tem nem eixo nomeado nem sample.
 */
FString Read(const UBlendSpace* BlendSpace);

} // namespace NodeScribeBlendSpace
