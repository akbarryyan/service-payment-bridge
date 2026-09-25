package httpserver

import (
	"context"
	"net/http"

	"github.com/labstack/echo/v4"
)

type Pinger interface {
	Ping(ctx context.Context) error
}

func HealthzHandler(pinger Pinger) echo.HandlerFunc {
	return func(c echo.Context) error {
		if err := pinger.Ping(c.Request().Context()); err != nil {
			return c.JSON(http.StatusServiceUnavailable, map[string]string{
				"status": "unavailable",
				"error":  err.Error(),
			})
		}
		return c.JSON(http.StatusOK, map[string]string{"status": "ok"})
	}
}
