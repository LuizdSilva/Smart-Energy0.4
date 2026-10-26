"""Cliente MQTT: recebe o estado das salas e envia comandos ao ESP32.

Roda numa thread própria do paho (loop_start), que reconecta sozinha. Cada mensagem recebida
abre uma conexão curta com o banco, então nada é compartilhado com o Flask.
"""
import json
import logging
import random

import paho.mqtt.client as mqtt

from . import banco_dados, regras_tempo_real

log = logging.getLogger("smartenergy.mqtt")
TAMANHO_MAXIMO = 2048  # bytes; o estado do ESP32 tem menos de 200


class ClienteMqtt:
    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.base = cfg["MQTT_BASE_TOPIC"]
        self.client = None
        self.conectado = False
        self.erro = None
        self.ultima_mensagem = None
        self.mensagens_ok = 0
        self.mensagens_descartadas = 0

    @property
    def topico_estado(self) -> str:
        return f"{self.base}/+/estado"

    def topico_comando(self, sala: str) -> str:
        return f"{self.base}/{regras_tempo_real.nome_no_topico(sala)}/comando"

    # ---- conexão
    def iniciar(self) -> None:
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                                  client_id=f"smartenergy-backend-{random.randint(0, 99999):05d}")
        self.client.on_connect = self._ao_conectar
        self.client.on_disconnect = self._ao_desconectar
        self.client.on_message = lambda client, userdata, msg: self.tratar_mensagem(msg.topic, msg.payload)
        self.client.reconnect_delay_set(min_delay=1, max_delay=30)
        try:
            self.client.connect_async(self.cfg["MQTT_HOST"], self.cfg["MQTT_PORT"], keepalive=30)
            self.client.loop_start()
        except Exception as exc:  # noqa: BLE001
            self.erro = str(exc)
            log.error("Não foi possível iniciar o MQTT: %s", exc)

    def parar(self) -> None:
        if self.client:
            self.client.loop_stop()
            self.client.disconnect()

    def _ao_conectar(self, client, userdata, flags, reason_code, properties=None):
        if reason_code.is_failure:
            self.conectado, self.erro = False, f"conexão recusada: {reason_code}"
            log.error(self.erro)
            return
        self.conectado, self.erro = True, None
        client.subscribe(self.topico_estado, qos=1)  # assina de novo a cada reconexão
        log.info("MQTT conectado, assinando %s", self.topico_estado)

    def _ao_desconectar(self, client, userdata, *args):
        self.conectado = False
        log.warning("MQTT desconectado")

    # ---- mensagens recebidas
    def tratar_mensagem(self, topico: str, conteudo: bytes) -> bool:
        """Valida e grava um estado. Devolve False se a mensagem foi descartada."""
        partes = topico.split("/")
        base = self.base.split("/")
        if len(partes) != len(base) + 2 or partes[:-2] != base or partes[-1] != "estado":
            return self._descartar(f"tópico inesperado: {topico}")
        if len(conteudo) > TAMANHO_MAXIMO:
            return self._descartar("mensagem grande demais")

        try:
            estado = regras_tempo_real.validar_estado(json.loads(conteudo.decode("utf-8")), topico_sala=partes[-2])
        except ValueError as exc:  # JSON inválido, texto inválido ou EstadoInvalido
            return self._descartar(f"{topico}: {exc}")

        conn = banco_dados.connect(self.cfg["DB_PATH"])
        try:
            regras_tempo_real.processar_estado(
                conn, estado,
                intervalo_gravacao_s=self.cfg["STORE_INTERVAL_S"],
                timeout_comando_s=self.cfg["COMMAND_TIMEOUT_S"],
            )
        except regras_tempo_real.SalaNaoCadastrada as exc:
            return self._descartar(f"{exc} (cadastre em Tempo Real > Cadastrar salas)")
        except Exception:  # noqa: BLE001  (um erro não pode derrubar a thread do MQTT)
            log.exception("erro ao processar o estado")
            return self._descartar("erro interno")
        finally:
            conn.close()

        self.mensagens_ok += 1
        self.ultima_mensagem = regras_tempo_real.data_para_texto(regras_tempo_real.agora_utc())
        return True

    def _descartar(self, motivo: str) -> bool:
        self.mensagens_descartadas += 1
        log.warning("mensagem MQTT descartada: %s", motivo)
        return False

    # ---- comandos e status
    def publicar_comando(self, sala: str, equipamento: str, acao: str) -> bool:
        if not self.client or not self.conectado:
            return False
        conteudo = json.dumps({"equipamento": equipamento, "acao": acao})
        return self.client.publish(self.topico_comando(sala), conteudo, qos=1).rc == mqtt.MQTT_ERR_SUCCESS

    def status(self) -> dict:
        return {
            "conectado": self.conectado,
            "broker": f"{self.cfg['MQTT_HOST']}:{self.cfg['MQTT_PORT']}",
            "topico_estado": self.topico_estado,
            "ultima_mensagem": self.ultima_mensagem,
            "mensagens_ok": self.mensagens_ok,
            "mensagens_descartadas": self.mensagens_descartadas,
            "erro": self.erro,
        }
