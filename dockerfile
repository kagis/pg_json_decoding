FROM alpine:3.10

ENV PGDATA=/var/lib/postgresql/data

RUN set -x \
 && install -o postgres -g postgres -m 755 -d $PGDATA /var/lib/postgresql/conf \
 && cd /tmp \
 && wget -qO- https://github.com/postgres/postgres/archive/REL_11_4.tar.gz | tar xz \
#  && wget -qO- https://github.com/postgres/postgres/archive/REL_12_1.tar.gz | tar xz \
 \
 && apk add --no-cache --virtual .build-deps \
  --repositories-file /dev/null \
  --repository https://mirror.ps.kz/alpine/v3.10/main \
  build-base \
  linux-headers \
  bison \
  flex \
  libxml2-dev \
  libxslt-dev \
  icu-dev \
  openssl-dev \
  autoconf \
  automake \
  libtool \
  clang-dev \
  llvm8-dev \
 \
 && cd /tmp/postgres-* \
 && ./configure \
  --prefix=/usr/local \
  --without-readline \
  --with-libxml \
  --with-libxslt \
  --with-icu \
  --with-openssl \
  --with-llvm \
 && make \
 && make install \
 && apk del .build-deps \
 && rm -r /tmp/* \
 \
 && apk add --no-cache --virtual .pg-runtime-deps \
  --repositories-file /dev/null \
  --repository https://mirror.ps.kz/alpine/v3.10/main \
  libxml2 \
  libxslt \
  icu \
  openssl \
  llvm8

RUN set -x \
 && apk add --no-cache --virtual .build-deps \
  --repositories-file /dev/null \
  --repository https://mirror.ps.kz/alpine/v3.10/main \
  build-base \
  clang \
  llvm8-dev \
 \
 && su postgres sh -c initdb \
 && echo wal_level=logical >> $PGDATA/postgresql.conf

CMD set -x \
 && cp -r /src /tmp/pg_json_decoding \
 && cd /tmp/pg_json_decoding \
 && make \
 && make install \
 && su postgres sh -c \
  'pg_ctl -w start \
  && psql -v ON_ERROR_STOP=1 -f test.sql \
  && pg_ctl -w stop'
