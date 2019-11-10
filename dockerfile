FROM alpine:3.10

ENV PGDATA=/var/lib/postgresql/data

RUN set -x \
 && install -o postgres -g postgres -m 755 -d $PGDATA /var/lib/postgresql/conf \
 && cd /tmp \
 && wget -qO- https://github.com/postgres/postgres/archive/REL_12_0.tar.gz | tar xz \
 \
 && apk add --no-cache --virtual .build-deps \
  --repositories-file /dev/null \
  --repository https://mirror.ps.kz/alpine/v3.10/main \
  --repository https://mirror.ps.kz/alpine/v3.10/community \
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
 \
 && apk add --no-cache \
  --repositories-file /dev/null \
  --repository https://mirror.ps.kz/alpine/v3.10/main \
  --repository https://mirror.ps.kz/alpine/v3.10/community \
  libxml2 libxslt icu openssl llvm8

USER postgres
RUN set -x \
 && initdb \
 \
 && printf %s\\n \
  #      db          user addr method
  "local all         all       trust" \
  "local replication all       trust" \
  "host  all         all  all  trust" \
  "host  replication all  all  trust" \
  > $PGDATA/pg_hba.conf \
 \
 && printf %s\\n \
  wal_level=logical \
  >> $PGDATA/postgresql.conf

USER root
RUN set -x \
 && apk add --no-cache --virtual .build-deps \
  --repositories-file /dev/null \
  --repository https://mirror.ps.kz/alpine/v3.10/main \
  --repository https://mirror.ps.kz/alpine/v3.10/community \
  build-base clang

# COPY *.c makefile /tmp/pg_json_decoding/
# USER postgres
CMD set -x \
 && cp -r /src /tmp/pg_json_decoding \
 && cd /tmp/pg_json_decoding \
 && make \
 && make install \
 && su postgres sh -c \
  'pg_ctl -w start \
  && psql -v ON_ERROR_STOP=1 -f test.sql \
  && pg_ctl -w stop'
