CREATE OR REPLACE FUNCTION prevent_final_status_change()
RETURNS TRIGGER AS $$
BEGIN
    IF OLD.status IN ('PAID', 'FAILED', 'EXPIRED', 'CANCELLED', 'REFUNDED')
       AND NEW.status IS DISTINCT FROM OLD.status THEN
        RAISE EXCEPTION 'Cannot change status from final state % to %', OLD.status, NEW.status;
    END IF;
    NEW.updated_at := now();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_prevent_final_status_change
    BEFORE UPDATE ON transactions
    FOR EACH ROW
    EXECUTE FUNCTION prevent_final_status_change();
