.DEFAULT_GOAL := build

GO ?= go
GO_RUN_TOOLS ?= $(GO) run -modfile ./tools/go.mod
GO_TEST = $(GO_RUN_TOOLS) gotest.tools/gotestsum --format pkgname


.PHONY: generate
generate:
	go generate ./...

.PHONY: fmt
fmt: ## Run go fmt against code.
	go run mvdan.cc/gofumpt -w .

.PHONY: vet
vet: ## Run go vet against code.
	go vet ./...

.PHONY: test
test: fmt vet ## Run tests.
	mkdir -p .test/reports
	$(GO_TEST) --junitfile .test/reports/unit-test.xml -- -race ./... -count=1 -short -cover -coverprofile .test/reports/unit-test-coverage.out

.PHONY: lint
lint: ## Run lint.
	$(GO_RUN_TOOLS) github.com/golangci/golangci-lint/cmd/golangci-lint run --timeout 5m -c .golangci.yml

.PHONY: clean
clean: ## Remove previous build.
	find . -type f -name '*.gen.go' -exec rm {} +
	git checkout go.mod

.PHONY: run-kafka
run-kafka:
	@docker run -d --rm  -p 2181:2181 -p 9092:9092 --name some-kafka --env ADVERTISED_HOST=127.0.0.1 --env ADVERTISED_PORT=9092 spotify/kafka

.PHONY: stop-kafka
stop-kafka:
	@docker stop some-kafka