all:
	podman build -t danceos/fail-base fail-base
	podman build -t danceos/fail-generic-tracing fail-generic-tracing
	podman build -t danceos/fail-demo fail-demo


run-fail-db:
	podman run --name fail-db \
		-e MYSQL_ROOT_PASSWORD=fail \
		-e MYSQL_USER=fail \
		-e MYSQL_PASSWORD=fail \
		-e MYSQL_DATABASE=fail \
		-d mysql

run-fail-demo:
	podman run --name fail-demo -p 127.0.0.1:5000:5000 --link fail-db:mysql -d danceos/fail-demo

ssh-fail-demo:
	ssh -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no fail@$$(podman inspect --format "{{ .NetworkSettings.IPAddress }}" fail-demo)
