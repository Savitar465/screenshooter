#!/bin/sh
# Genera el par de claves Ed25519 con el que se firman las sumas (SHA256SUMS) de cada release. QAflow
# sólo se instala sola un paquete cuyas sumas lleven una firma válida de esta clave.
#
#   packaging/release/update-signing-key.sh [fichero.pem]
#
# La clave privada no se sube nunca al repositorio: va al secreto QAFLOW_UPDATE_SIGNING_KEY de GitHub,
# con el que firma el job `release` del CI. La pública (base64) va en QAFLOW_UPDATE_PUBLIC_KEY, en el
# CMakeLists.txt raíz, y se compila dentro de QAflow. Cambiar de clave deja sin actualizaciones
# automáticas a las versiones ya publicadas con la anterior: guárdala bien.
set -eu

OUT="${1:-qaflow-update-signing-key.pem}"
if [ -e "$OUT" ]; then
    echo "$OUT ya existe: no se sobrescribe" >&2
    exit 1
fi

umask 077
openssl genpkey -algorithm ed25519 -out "$OUT"
# La clave pública en DER termina con sus 32 bytes en bruto, que es lo que lee QAflow.
PUBLIC="$(openssl pkey -in "$OUT" -pubout -outform DER | tail -c 32 | base64)"

cat <<MSG
Clave privada: $OUT (no la subas al repositorio y guarda una copia aparte)

1. Guárdala como secreto del repositorio:
     gh secret set QAFLOW_UPDATE_SIGNING_KEY < "$OUT"

2. Pon la clave pública en el CMakeLists.txt raíz:
     set(QAFLOW_UPDATE_PUBLIC_KEY "$PUBLIC" CACHE STRING ...)
MSG
